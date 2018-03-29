//
// Copyright (c) 2018 XiaoMi All rights reserved.
//

#include "mace/kernels/depth_to_space.h"
#include "mace/core/runtime/opencl/cl2_header.h"
#include "mace/core/runtime/opencl/opencl_runtime.h"
#include "mace/kernels/opencl/helper.h"
#include "mace/utils/tuner.h"
#include "mace/utils/utils.h"

namespace mace {
namespace kernels {

template <typename T>
void DepthToSpaceOpFunctor<DeviceType::OPENCL, T>::operator()(
    const Tensor *input, Tensor *output, StatsFuture *future) {
  const index_t batch = input->dim(0);
  const index_t input_height = input->dim(1);
  const index_t input_width = input->dim(2);
  const index_t input_depth = input->dim(3);

  const char *kernel_name = nullptr;
  index_t kernel_width = input_width;

  index_t output_height, output_width, output_depth;
  if (d2s_) {
    output_height = input_height * block_size_;
    output_width = input_width * block_size_;
    output_depth = input_depth / (block_size_ * block_size_);
    kernel_name = "depth_to_space";
    kernel_width = output_width;
  } else {
    output_height = input_height / block_size_;
    output_width = input_width / block_size_;
    output_depth = input_depth * block_size_ * block_size_;
    kernel_name = "space_to_depth";
    kernel_width = input_width;
  }
  const index_t input_depth_blocks = RoundUpDiv4(input_depth);
  const index_t output_depth_blocks = RoundUpDiv4(output_depth);

  std::vector<index_t> output_shape = {batch, output_height, output_width,
                                       output_depth};

  std::vector<size_t> image_shape;
  CalImage2DShape(output_shape, BufferType::IN_OUT_CHANNEL, &image_shape);
  output->ResizeImage(output_shape, image_shape);

  if (kernel_.get() == nullptr) {
    auto runtime = OpenCLRuntime::Global();
    std::set<std::string> built_options;
    std::string obfuscated_kernel_name = MACE_OBFUSCATE_SYMBOL(kernel_name);
    std::stringstream kernel_name_ss;
    kernel_name_ss << "-D" << kernel_name << "=" << obfuscated_kernel_name;
    built_options.emplace(kernel_name_ss.str());
    auto dt = DataTypeToEnum<T>::value;
    built_options.emplace("-DDATA_TYPE=" + DtToCLDt(dt));
    built_options.emplace("-DCMD_DATA_TYPE=" + DtToCLCMDDt(dt));
    kernel_ =
        runtime->BuildKernel("depth_to_space",
                             obfuscated_kernel_name, built_options);
  }
  if (!IsVecEqual(input_shape_, input->shape())) {
    uint32_t idx = 0;
    kernel_.setArg(idx++, *(input->opencl_image()));
    kernel_.setArg(idx++, static_cast<int32_t>(block_size_));
    kernel_.setArg(idx++, static_cast<int32_t>(input_height));
    kernel_.setArg(idx++, static_cast<int32_t>(input_width));
    kernel_.setArg(idx++, static_cast<int32_t>(input_depth_blocks));
    kernel_.setArg(idx++, static_cast<int32_t>(output_height));
    kernel_.setArg(idx++, static_cast<int32_t>(output_width));
    kernel_.setArg(idx++, static_cast<int32_t>(output_depth_blocks));
    kernel_.setArg(idx++, *(output->opencl_image()));
    input_shape_ = input->shape();
  }

  if (d2s_) {
    const uint32_t gws[3] = {static_cast<uint32_t>(output_depth_blocks),
                             static_cast<uint32_t>(output_width),
                             static_cast<uint32_t>(output_height * batch)};
    const std::vector<uint32_t> lws = {8, 16, 8, 1};
    std::stringstream ss;
    ss << "depth_to_space_opencl_kernel_" << output->dim(0) << "_"
       << output->dim(1) << "_" << output->dim(2) << "_" << output_depth_blocks;
    TuningOrRun3DKernel(kernel_, ss.str(), gws, lws, future);
  } else {
    const uint32_t gws[3] = {static_cast<uint32_t>(input_depth_blocks),
                             static_cast<uint32_t>(input_width),
                             static_cast<uint32_t>(input_height * batch)};
    const std::vector<uint32_t> lws = {8, 16, 8, 1};
    std::stringstream ss;
    ss << "depth_to_space_opencl_kernel_" << input->dim(0) << "_"
       << input->dim(1) << "_" << input->dim(2) << "_" << input_depth_blocks;
    TuningOrRun3DKernel(kernel_, ss.str(), gws, lws, future);
  }
}

template struct DepthToSpaceOpFunctor<DeviceType::OPENCL, float>;
template struct DepthToSpaceOpFunctor<DeviceType::OPENCL, half>;

}  // namespace kernels
}  // namespace mace
