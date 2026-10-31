#include "hand_model.h"

#include "dl_detect_base.hpp"
#include "dl_detect_espdet_postprocessor.hpp"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <new>

namespace snap_sense {
namespace {

constexpr const char* kTag = "HandModel";
constexpr float kScoreThreshold = 0.5f;
constexpr std::size_t kMinPsramBlock = 2 * 1024 * 1024;
constexpr std::size_t kMinInternalFree = 64 * 1024;
extern const std::uint8_t kModelData[] asm("_binary_hand_detect_espdl_start");

bool valid_tensor(const dl::TensorBase* tensor)
{
    return tensor && tensor->data &&
           (tensor->dtype == dl::DATA_TYPE_INT8 || tensor->dtype == dl::DATA_TYPE_INT16);
}

}  // namespace

class HandModel::CheckedDetector : public dl::detect::DetectImpl {
public:
    CheckedDetector()
    {
        m_model = nullptr;
        m_image_preprocessor = nullptr;
        m_postprocessor = nullptr;
    }

    esp_err_t init()
    {
        // The vendor Model constructor can still fail inside its own allocation
        // code before returning. Preflight and checked tensors do not make every
        // vendor OOM/assert path recoverable.
        m_model = new dl::Model(reinterpret_cast<const char*>(kModelData),
                                "espdet_pico_224_224_hand.espdl",
                                fbs::MODEL_LOCATION_IN_FLASH_RODATA);
        if (!m_model->get_fbs_model()) return ESP_ERR_INVALID_RESPONSE;

        const auto& inputs = m_model->get_inputs();
        const auto& outputs = m_model->get_outputs();
        if (inputs.size() != 1) return ESP_ERR_INVALID_RESPONSE;
        const auto* input = inputs.begin()->second;
        if (!valid_tensor(input) || input->dtype != dl::DATA_TYPE_INT8 ||
            input->shape.size() != 4 || input->shape[0] != 1 || input->shape[1] != 224 ||
            input->shape[2] != 224 || input->shape[3] != 3) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        for (const auto& output : outputs) {
            if (!valid_tensor(output.second)) return ESP_ERR_INVALID_RESPONSE;
        }
        constexpr const char* box_names[] = {"box0", "box1", "box2"};
        constexpr const char* score_names[] = {"score0", "score1", "score2"};
        for (std::size_t stage = 0; stage < 3; ++stage) {
            const auto box_it = outputs.find(box_names[stage]);
            const auto score_it = outputs.find(score_names[stage]);
            if (box_it == outputs.end() || score_it == outputs.end()) return ESP_ERR_INVALID_RESPONSE;
            const auto* box = box_it->second;
            const auto* score = score_it->second;
            if (box->shape.size() != 4 || score->shape.size() != 4 ||
                box->shape[0] != 1 || score->shape[0] != 1 ||
                box->shape[1] <= 0 || box->shape[2] <= 0 || box->shape[3] != 4 ||
                score->shape[1] != box->shape[1] || score->shape[2] != box->shape[2] ||
                score->shape[3] != 1 || score->dtype != box->dtype) {
                return ESP_ERR_INVALID_RESPONSE;
            }
        }

        m_model->minimize();
        m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {255, 255, 255});
        m_image_preprocessor->enable_letterbox({114, 114, 114});
        m_postprocessor = new dl::detect::ESPDetPostProcessor(
            m_model, m_image_preprocessor, kScoreThreshold, 0.5f, 10,
            {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});
        return ESP_OK;
    }
};

HandModel::~HandModel()
{
    delete detector_;
}

esp_err_t HandModel::init()
{
    if (detector_) return ESP_OK;

    const auto heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const auto internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const auto psram_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (psram_block < kMinPsramBlock || internal_free < kMinInternalFree) {
        ESP_LOGW(kTag, "preflight refused: psram_block=%u internal_free=%u",
                 static_cast<unsigned>(psram_block), static_cast<unsigned>(internal_free));
        return ESP_ERR_NO_MEM;
    }

    try {
        auto candidate = std::make_unique<CheckedDetector>();
        const auto error = candidate->init();
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "model tensors invalid: %s", esp_err_to_name(error));
            return error;
        }
        const auto memory = candidate->get_raw_model()->get_memory_info();
        const auto total = memory.at("total");
        ESP_LOGI(kTag, "loaded: internal=%u psram=%u heap_before=%u heap_after=%u",
                 static_cast<unsigned>(total.internal), static_cast<unsigned>(total.psram),
                 static_cast<unsigned>(heap_before),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
        detector_ = candidate.release();
        return ESP_OK;
    } catch (const std::bad_alloc&) {
        ESP_LOGE(kTag, "model allocation failed");
        return ESP_ERR_NO_MEM;
    } catch (const std::exception& error) {
        ESP_LOGE(kTag, "model initialization failed: %s", error.what());
        return ESP_FAIL;
    } catch (...) {
        ESP_LOGE(kTag, "model initialization failed");
        return ESP_FAIL;
    }
}

esp_err_t HandModel::infer(const std::uint8_t* rgb, std::size_t width, std::size_t height,
                           HandDetections& detections)
{
    detections = {};
    if (!rgb || width == 0 || height == 0 ||
        width > std::numeric_limits<std::uint16_t>::max() ||
        height > std::numeric_limits<std::uint16_t>::max() ||
        width > static_cast<std::size_t>(std::numeric_limits<int>::max()) / height / 3) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!detector_) return ESP_ERR_INVALID_STATE;

    try {
        const dl::image::img_t image = {
            .data = const_cast<std::uint8_t*>(rgb),
            .width = static_cast<std::uint16_t>(width),
            .height = static_cast<std::uint16_t>(height),
            .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888,
        };
        // DetectImpl calls Model::run() with its SINGLE_CORE default.
        const auto& results = detector_->run(image);
        for (const auto& result : results) {
            if (!std::isfinite(result.score) || result.score < kScoreThreshold || result.score > 1.0f ||
                result.box.size() != 4 || result.box[0] < 0 || result.box[1] < 0 ||
                result.box[0] >= result.box[2] || result.box[1] >= result.box[3] ||
                static_cast<std::size_t>(result.box[2]) >= width ||
                static_cast<std::size_t>(result.box[3]) >= height) {
                continue;
            }
            std::size_t position = 0;
            while (position < detections.count && detections.boxes[position].score >= result.score) ++position;
            if (position == detections.boxes.size()) continue;
            if (detections.count < detections.boxes.size()) ++detections.count;
            for (std::size_t i = detections.count - 1; i > position; --i) {
                detections.boxes[i] = detections.boxes[i - 1];
            }
            detections.boxes[position] = {
                true, result.score, result.box[0], result.box[1], result.box[2], result.box[3]};
        }
        return ESP_OK;
    } catch (const std::bad_alloc&) {
        detections = {};
        return ESP_ERR_NO_MEM;
    } catch (const std::exception& error) {
        detections = {};
        ESP_LOGE(kTag, "inference failed: %s", error.what());
        return ESP_FAIL;
    } catch (...) {
        detections = {};
        return ESP_FAIL;
    }
}

}  // namespace snap_sense
