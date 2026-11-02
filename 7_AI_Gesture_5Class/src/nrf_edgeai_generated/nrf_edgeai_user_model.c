/* 2026-10-22T05:38:43.528180 */
/*
* Copyright (c) 2026 Nordic Semiconductor ASA
* SPDX-License-Identifier: Apache-2.0
*/
#include "nrf_edgeai_user_model.h"
#include "nrf_edgeai_user_types.h"
#include <nrf_edgeai/nrf_edgeai_platform.h>
#include <nrf_edgeai/rt/private/nrf_edgeai_interfaces.h>
#include <assert.h>

//////////////////////////////////////////////////////////////////////////////
/* Nordic EdgeAI Lab Solution ID and Runtime Version */
#define EDGEAI_LAB_SOLUTION_ID_STR      "96964"
#define EDGEAI_RUNTIME_VERSION_COMBINED 0x00000003

//////////////////////////////////////////////////////////////////////////////
#define INPUT_TYPE                         f32

/** User input features type */
#define INPUT_FEATURE_DATA_TYPE            NRF_EDGEAI_INPUT_F32

/** Number of unique features in the original input sample */
#define INPUT_UNIQ_FEATURES_NUM            6

/** Number of unique features actually used by NN from the original input sample */
#define INPUT_UNIQ_FEATURES_USED_NUM       6

/** Number of input feature samples that should be collected in the input window
 *  feature_sample = 1 * INPUT_UNIQ_FEATURES_NUM
 */
#define INPUT_WINDOW_SIZE                  200

/** Number of input feature samples on that the input window is shifted */
#define INPUT_WINDOW_SHIFT                 33

/** Number of subwindows in input feature window,
* the SUBWINDOW_SIZE = INPUT_WINDOW_SIZE / INPUT_SUBWINDOW_NUM
* if the window size is not divisible by the number of subwindows without a remainder,
* the remainder is added to the last subwindow size */
#define INPUT_SUBWINDOW_NUM                 0

#define INPUT_UNIQUE_SCALES_NUM (sizeof(INPUT_FEATURES_SCALE_MIN) / sizeof(INPUT_FEATURES_SCALE_MIN[0])) 

/** Defines input(also used for LAG) features MIN scaling factor
 */
static const nrf_user_input_t INPUT_FEATURES_SCALE_MIN[] = {
 -24235704.0000000, -14589039.0000000, -22015164.0000000, -6776937.0000000,
 -5761068.0000000, -9178859.0000000 };

/** Defines input(also used for LAG) features MAX scaling factor
 */
static const nrf_user_input_t INPUT_FEATURES_SCALE_MAX[] = {
 13423734.0000000, 21978076.0000000, 21006588.0000000, 6476391.0000000,
 5940663.0000000, 7827625.0000000 };

/** Defines which unique features from the input data will be used/collected,
 *  one bit for one unique feature, starting from LSB
 */
#define INPUT_FEATURES_USAGE_MASK NULL

/** Defines which unique input features is used for LAG features processing,
 *  one bit for one unique feature, starting from LSB
 */
#define INPUT_FEATURES_USED_FOR_LAGS_MASK NULL

//////////////////////////////////////////////////////////////////////////////
#define MODEL_TYPE                 __NRF_EDGEAI_MODEL_AXON
#define MODEL_TASK                 0
#define MODEL_OUTPUTS_NUM          5

#define MODEL_USES_AS_INPUT_INPUT_FEATURES 0
#define MODEL_USES_AS_INPUT_DSP_FEATURES 1
#define MODEL_USES_AS_INPUT_MASK ((MODEL_USES_AS_INPUT_INPUT_FEATURES << 0) | (MODEL_USES_AS_INPUT_DSP_FEATURES << 1)) 

#if MODEL_TYPE == __NRF_EDGEAI_MODEL_AXON 
#include <drivers/axon/nrf_axon_nn_infer.h>  
#include <axon/nrf_axon_platform.h> 
#include "nrf_edgeai_user_model_axon.h" 
#define P_MODEL_INSTANCE &model_axon_user_instance_96964
#else  // MODEL_TYPE == __NRF_EDGEAI_MODEL_NEUTON
#define P_MODEL_INSTANCE &model_neuton_user_instance_ 
#endif


#define NN_DECODED_OUTPUT_INIT                 \
.classif = {                                   \
   .predicted_class = 0,                       \
   .num_classes = MODEL_OUTPUTS_NUM,           \
}

//////////////////////////////////////////////////////////////////////////////
/** Input feature buffer element size, 
 * if quantization of model is bigger than input features size in bits, 
 * the size of input buffer should aligned to nrf_user_neuron_t */ 
#define INPUT_TYPE_SIZE \
    ((sizeof(nrf_user_input_t) > sizeof(nrf_user_neuron_t)) ? sizeof(nrf_user_input_t) : sizeof(nrf_user_neuron_t)) 

/** Input features window size in bytes to allocate statically */ 
#define INPUT_WINDOW_BUFFER_SIZE_BYTES \
    (INPUT_WINDOW_SIZE * INPUT_UNIQ_FEATURES_NUM * INPUT_TYPE_SIZE) 

static uint8_t input_window_[INPUT_WINDOW_BUFFER_SIZE_BYTES] __NRF_EDGEAI_ALIGNED; 

#define INPUT_WINDOW_MEMORY    &input_window_[0] 

static nrf_edgeai_window_ctx_t input_window_ctx_; 
#define P_INPUT_WINDOW_CTX     &input_window_ctx_ 

//////////////////////////////////////////////////////////////////////////////
/** The maximum number of extracted features that user used for all unique input features */
#define EXTRACTED_FEATURES_NUM  78

#define EXTRACTED_FEATURES_META_TYPE f32 

/** DSP feature buffer element size,
 * if quantization of model is bigger than DSP features size in bits,
 * the size of extracted DSP features buffer should aligned to nrf_user_neuron_t */
#define EXTRACTED_FEATURE_SIZE_BYTES                                                  \
    ((sizeof(nrf_user_feature_t) > sizeof(nrf_user_neuron_t)) ? sizeof(nrf_user_feature_t) : \
                                                            sizeof(nrf_user_neuron_t))

/** Size of extracted features buffer in bytes */
#define EXTRACTED_FEATURES_BUFFER_SIZE_BYTES (EXTRACTED_FEATURES_NUM * EXTRACTED_FEATURE_SIZE_BYTES) 

/** Defines feature extraction masks used as nrf_edgeai_features_mask_t,
 *  64 bit for one unique input feature, @ref nrf_edgeai_features_mask_t to see bitmask
 */

static const uint64_t FEATURES_EXTRACTION_MASK[] = {
 0xc00c79f00000000, 0xc00c79f00000000, 0xc00c79f00000000,
 0xc00c79f00000000, 0xc00c79f00000000, 0xc00c79f00000000 };

/** Defines arguments used while feature extraction
 */

/** Defines arguments used while feature extraction
 */
#define FEATURES_EXTRACTION_ARGUMENTS NULL

/** Defines extracted features MIN scaling factor
 */
static const nrf_user_feature_t EXTRACTED_FEATURES_SCALE_MIN[] = {
 -24235704.0000000, -9857232.0000000, 86141.0000000, -9905760.0000000,
 14255.5751953, 17696.7890625, 116399.5156250, 0.0050251, 0.0000000,
 114735.3515625, 20483.2910156, -36523.7148438, -9907339.0000000,
 -14589039.0000000, -9822536.0000000, 80159.0000000, -9928164.0000000,
 11815.1796875, 14959.1679688, 310988.8750000, 0.0050251, 0.0000000,
 296978.7500000, 16918.1406250, -66966.4531250, -12456390.0000000,
 -22015164.0000000, -9189635.0000000, 98106.0000000, -9240008.0000000,
 15688.9228516, 19489.2675781, 487052.8437500, 0.0100503, 0.0000000,
 486502.1562500, 21084.5332031, -48198.2734375, -9550983.0000000,
 -6776937.0000000, -21380.0000000, 5498.0000000, -931190.2500000,
 803.8033447, 1023.9800415, 22265.4121094, 0.0050251, 0.0000000,
 18450.6894531, 951.4221191, -21597.7148438, -1810299.0000000,
 -5761068.0000000, -13439.0000000, 6108.0000000, -707479.1875000,
 880.7022705, 1144.4777832, 12998.0410156, 0.0050251, 0.0000000,
 10607.1904297, 1037.6783447, -13603.2578125, -1844607.7500000,
 -9178859.0000000, 32375.0000000, 4887.0000000, -678325.9375000,
 708.3643799, 910.9446411, 30359.6582031, 0.0050251, 0.0000000,
 28621.5507812, 865.5879517, -10651.1767578, -2441300.0000000 };

/** Defines extracted features MAX scaling factor
 */
static const nrf_user_feature_t EXTRACTED_FEATURES_SCALE_MAX[] = {
 7286144.0000000, 13423734.0000000, 31659434.0000000, 7343332.0000000,
 4453795.0000000, 6230947.5000000, 9943979.0000000, 0.6180905, 0.1859296,
 9905760.0000000, 877450.5625000, 31082.1328125, 7665560.0000000,
 5522634.0000000, 21978076.0000000, 28226932.0000000, 8071282.5000000,
 6896429.5000000, 8163328.5000000, 9928244.0000000, 0.5778894, 0.0402010,
 9928164.0000000, 407627.5312500, 100899.4531250, 8668060.0000000,
 9918249.0000000, 21006588.0000000, 32866614.0000000, 9977969.0000000,
 3601615.2500000, 5589400.5000000, 9985284.0000000, 0.6683417, 0.0804020,
 9977969.0000000, 679152.8125000, 26840.6113281, 10534969.0000000,
 -25656.0000000, 6476391.0000000, 11235030.0000000, 370242.2500000,
 1665754.2500000, 2431047.7500000, 2443793.2500000, 0.4974874, 0.1407035,
 1586263.2500000, 182218.8281250, 13366.1787109, 1353423.5000000,
 -18325.0000000, 5940663.0000000, 7217370.0000000, 1097495.3750000,
 1181231.2500000, 1487837.7500000, 1722231.7500000, 0.7085427, 0.2160804,
 1130796.0000000, 143930.7656250, 19719.3554688, 2005555.2500000,
 28710.0000000, 7827625.0000000, 16148218.0000000, 640882.7500000,
 2214915.2500000, 3394194.5000000, 3394738.0000000, 0.5226130, 0.1005025,
 2187742.5000000, 231272.2968750, 18801.5546875, 1451732.7500000 };

/** Memory allocation to store extracted features during DSP pipeline */
static uint8_t extracted_features_buffer_[EXTRACTED_FEATURES_BUFFER_SIZE_BYTES] __NRF_EDGEAI_ALIGNED;


/** Timedomain features processing context  */
#define P_TIMEDOMAIN_FEATURES_CTX  NULL
/** Timedomain features in feature extraction pipeline  */
static const nrf_edgeai_features_pipeline_func_f32_t timedomain_features_[] = {
    nrf_edgeai_feature_utility_tss_sum_f32,
    nrf_edgeai_feature_min_max_range_f32,
    nrf_edgeai_feature_mean_f32,
    nrf_edgeai_feature_mad_f32,
    nrf_edgeai_feature_std_f32,
    nrf_edgeai_feature_rms_f32,
    nrf_edgeai_feature_mcr_f32,
    nrf_edgeai_feature_zcr_f32,
    nrf_edgeai_feature_absmean_f32,
    nrf_edgeai_feature_amdf_f32,
    nrf_edgeai_feature_lrp_f32
 };

static const nrf_edgeai_features_pipeline_ctx_t timedomain_pipeline_ = {
    .functions_num     = sizeof(timedomain_features_) / sizeof(timedomain_features_[0]),
    .functions.p_void  = timedomain_features_,
    .p_ctx             = P_TIMEDOMAIN_FEATURES_CTX,
};
#define P_TIMEDOMAIN_PIPELINE &timedomain_pipeline_ 

#define P_FREQDOMAIN_PIPELINE NULL

#define P_CUSTOMDOMAIN_PIPELINE NULL

static nrf_edgeai_dsp_pipeline_t dsp_pipeline_ = { 
   .features = {  
       .p_masks = (const nrf_edgeai_features_mask_t*)FEATURES_EXTRACTION_MASK, 
       .buffer.p_void = extracted_features_buffer_, 
       .overall_num = EXTRACTED_FEATURES_NUM, 
       .masks_num = sizeof(FEATURES_EXTRACTION_MASK) / sizeof(FEATURES_EXTRACTION_MASK[0]), 

       .p_timedomain_pipeline = P_TIMEDOMAIN_PIPELINE, 
       .p_freqdomain_pipeline = P_FREQDOMAIN_PIPELINE, 
       .p_customdomain_pipeline = P_CUSTOMDOMAIN_PIPELINE, 

       .meta.EXTRACTED_FEATURES_META_TYPE = { 
           .p_min = EXTRACTED_FEATURES_SCALE_MIN, 
           .p_max = EXTRACTED_FEATURES_SCALE_MAX, 
       .p_arguments = FEATURES_EXTRACTION_ARGUMENTS, 
       },
   }, 
}; 

#define P_DSP_PIPELINE         &dsp_pipeline_ 


//////////////////////////////////////////////////////////////////////////////
#define NN_INPUT_INIT_INTERFACE        nrf_edgeai_input_init_sliding_window 
#define NN_INPUT_FEED_INTERFACE        nrf_edgeai_input_feed_sliding_window_f32 
#define NN_PROCESS_FEATURES_INTERFACE  nrf_edgeai_process_features_dsp_f32 
#define NN_SCALE_FEATURES_INTERFACE    nrf_edgeai_scale_features_dsp_f32_f32 
#define NN_INIT_INFERENCE_INTERFACE    nrf_edgeai_init_inference_axon 
#define NN_RUN_INFERENCE_INTERFACE     nrf_edgeai_run_inference_axon 
#define NN_PROPAGATE_OUTPUTS_INTERFACE nrf_edgeai_output_dequantize_axon_q8_f32 
#define NN_DECODE_OUTPUTS_INTERFACE    nrf_edgeai_output_decode_classification_f32 

//////////////////////////////////////////////////////////////////////////////

static nrf_user_output_t model_outputs_[MODEL_OUTPUTS_NUM];

//////////////////////////////////////////////////////////////////////////////

static nrf_edgeai_t nrf_edgeai_ = {
    ///
    .metadata.p_solution_id     = EDGEAI_LAB_SOLUTION_ID_STR,
    .metadata.version.combined  = EDGEAI_RUNTIME_VERSION_COMBINED,
    ///   
    .input.p_used_for_lags_mask = INPUT_FEATURES_USED_FOR_LAGS_MASK,
    .input.p_usage_mask         = INPUT_FEATURES_USAGE_MASK,
    .input.type                 = INPUT_FEATURE_DATA_TYPE,
    .input.unique_num           = INPUT_UNIQ_FEATURES_NUM,
    .input.unique_num_used      = INPUT_UNIQ_FEATURES_USED_NUM,
    .input.unique_scales_num    = INPUT_UNIQUE_SCALES_NUM,
    .input.window_size          = INPUT_WINDOW_SIZE,
    .input.window_shift         = INPUT_WINDOW_SHIFT,
    .input.subwindow_num        = INPUT_SUBWINDOW_NUM,
    .input.window_memory.p_void = INPUT_WINDOW_MEMORY,
    .input.p_window_ctx         = P_INPUT_WINDOW_CTX,

    .input.scale.INPUT_TYPE = {
        .p_min = INPUT_FEATURES_SCALE_MIN,
        .p_max = INPUT_FEATURES_SCALE_MAX,
    }, 
    ///
    .p_dsp = P_DSP_PIPELINE,
    ///
    .model.type                 = (nrf_edgeai_model_type_t)MODEL_TYPE,
    .model.task                 = (nrf_edgeai_model_task_t)MODEL_TASK,
    .model.instance.p_void      = P_MODEL_INSTANCE,
    .model.output.memory.p_void = model_outputs_,
    .model.output.num           = MODEL_OUTPUTS_NUM,
    .model.uses_as_input.all    = MODEL_USES_AS_INPUT_MASK,
    ///
    .interfaces.input_init          = NN_INPUT_INIT_INTERFACE,
    .interfaces.feed_inputs         = NN_INPUT_FEED_INTERFACE,
    .interfaces.process_features    = NN_PROCESS_FEATURES_INTERFACE,
    .interfaces.scale_features      = NN_SCALE_FEATURES_INTERFACE,
    .interfaces.init_inference      = NN_INIT_INFERENCE_INTERFACE,
    .interfaces.run_inference       = NN_RUN_INFERENCE_INTERFACE,
    .interfaces.propagate_outputs   = NN_PROPAGATE_OUTPUTS_INTERFACE,
    .interfaces.decode_outputs      = NN_DECODE_OUTPUTS_INTERFACE,
    ///
    .decoded_output = { NN_DECODED_OUTPUT_INIT },
};

//////////////////////////////////////////////////////////////////////////////

nrf_edgeai_t* nrf_edgeai_user_model_96964(void)
{
    return &nrf_edgeai_;
}

//////////////////////////////////////////////////////////////////////////////

uint32_t nrf_edgeai_user_model_size_96964(void)
{
    uint32_t model_size = 0;

#if MODEL_TYPE == __NRF_EDGEAI_MODEL_NEUTON
    model_size +=
        (sizeof(MODEL_WEIGHTS) + sizeof(MODEL_NEURONS_LINKS) +
         sizeof(MODEL_NEURON_EXTERNAL_LINKS_NUM) + sizeof(MODEL_NEURON_INTERNAL_LINKS_NUM) +
         sizeof(MODEL_NEURON_ACTIVATION_WEIGHTS) + sizeof(MODEL_NEURON_ACTIVATION_TYPE_MASK) +
         sizeof(MODEL_OUTPUT_NEURONS_INDICES));

#if MODEL_TASK == __NRF_EDGEAI_TASK_ANOMALY_DETECTION
    model_size += sizeof(MODEL_AVERAGE_EMBEDDING) + sizeof(MODEL_OUTPUT_SCALE_MIN) +
                  sizeof(MODEL_OUTPUT_SCALE_MAX);
#endif

#if MODEL_TASK == __NRF_EDGEAI_TASK_REGRESSION
    model_size += sizeof(MODEL_OUTPUT_SCALE_MIN) + sizeof(MODEL_OUTPUT_SCALE_MAX);
#endif

#elif MODEL_TYPE == __NRF_EDGEAI_MODEL_AXON
    const nrf_axon_nn_compiled_model_s* p_axon_model = P_MODEL_INSTANCE;

    model_size += sizeof(*p_axon_model);
    model_size += p_axon_model->model_const_size;
    model_size += p_axon_model->cmd_buffer_len * sizeof(NRF_AXON_PLATFORM_BITWIDTH_UNSIGNED_TYPE);

    if (p_axon_model->persistent_vars.buf_ptr != NULL)
    {
        model_size +=
            sizeof(nrf_axon_nn_model_persistent_var_s) * p_axon_model->persistent_vars.count;
    }

#endif

    return model_size;
}


