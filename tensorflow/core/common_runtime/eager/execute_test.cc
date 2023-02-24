/* Copyright 2022 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include "tensorflow/core/common_runtime/eager/execute.h"

#include <memory>
#include <vector>

#include "tensorflow/core/framework/tensor_testutil.h"
#include "tensorflow/core/lib/core/status_test_util.h"
#include "tensorflow/core/lib/monitoring/cell_reader.h"
#include "tensorflow/core/platform/test.h"

namespace tensorflow {
namespace {

TEST(ExecuteTest, EagerOperationAsFunction) {
  StaticDeviceMgr device_mgr(
      DeviceFactory::NewDevice("CPU", {}, "/job:localhost/replica:0/task:0"));
  auto ctx = new EagerContext(
      SessionOptions(),
      tensorflow::ContextDevicePlacementPolicy::DEVICE_PLACEMENT_EXPLICIT,
      false, &device_mgr, false, nullptr, nullptr);
  ctx->SetRunEagerOpAsFunction(true);

  auto op = std::make_unique<EagerOperation>(ctx);
  TF_ASSERT_OK(op->Reset(
      /*op=*/"Mul",
      /*raw_device_name=*/"/job:localhost/replica:0/task:0/device:CPU:0"));

  Tensor input1_tensor = test::AsScalar<int64_t>(3);
  auto input1 = core::RefCountPtr<ImmediateExecutionTensorHandle>(
      ctx->CreateLocalHandleFromTFTensor(input1_tensor,
                                         ctx->HostCPUName().c_str()));
  TF_ASSERT_OK(op->AddInput(input1.get()));
  Tensor input2_tensor = test::AsScalar<int64_t>(2);
  auto input2 = core::RefCountPtr<ImmediateExecutionTensorHandle>(
      ctx->CreateLocalHandleFromTFTensor(input2_tensor,
                                         ctx->HostCPUName().c_str()));
  TF_ASSERT_OK(op->AddInput(input2.get()));

  std::vector<TensorHandle*> retvals(1);
  int num_retvals = retvals.size();
  TF_ASSERT_OK(EagerExecute(op.get(), retvals.data(), &num_retvals));

  retvals[0]->Unref();
  retvals[0] = nullptr;
  ctx->Unref();
}

TEST(ExecuteTest, SimpleFunction) {
  StaticDeviceMgr device_mgr(
      DeviceFactory::NewDevice("CPU", {}, "/job:localhost/replica:0/task:0"));
  auto ctx = new EagerContext(
      SessionOptions(),
      tensorflow::ContextDevicePlacementPolicy::DEVICE_PLACEMENT_EXPLICIT,
      false, &device_mgr, false, nullptr, nullptr);

  const Tensor kTwo = test::AsScalar<int64_t>(2);
  const string function_name = "XTimesTwo";
  const FunctionDef x_times_two = FunctionDefHelper::Define(
      // Name
      function_name,
      // Args
      {"x: int64"},
      // Return values
      {"y: int64"},
      // Attr def
      {},
      // Nodes
      {
          {{"two"}, "Const", {}, {{"value", kTwo}, {"dtype", DT_INT64}}},
          {{"scale"},
           "Cast",
           {"two"},
           {{"SrcT", DT_INT64}, {"DstT", DT_INT64}}},
          {{"y"}, "Mul", {"x", "scale"}, {{"T", DT_INT64}}},
      });
  TF_ASSERT_OK(ctx->AddFunctionDef(x_times_two));

  auto op = std::make_unique<EagerOperation>(ctx);
  TF_ASSERT_OK(op->Reset(
      /*op=*/function_name.c_str(),
      /*raw_device_name=*/"/job:localhost/replica:0/task:0/device:CPU:0"));

  Tensor input_tensor = test::AsScalar<int64_t>(3);
  auto input = core::RefCountPtr<ImmediateExecutionTensorHandle>(
      ctx->CreateLocalHandleFromTFTensor(input_tensor,
                                         ctx->HostCPUName().c_str()));
  TF_ASSERT_OK(op->AddInput(input.get()));

  monitoring::testing::CellReader<int64_t> counter_reader(
      "/tensorflow/core/tf_function_compile");
  std::vector<TensorHandle*> retvals(1);
  int num_retvals = retvals.size();
  TF_ASSERT_OK(EagerExecute(op.get(), retvals.data(), &num_retvals));
  EXPECT_EQ(counter_reader.Delta("CPU", "disabled"), 1);

  retvals[0]->Unref();
  retvals[0] = nullptr;
  ctx->Unref();
}

TEST(ExecuteTest, CompiledFunction) {
  StaticDeviceMgr device_mgr(
      DeviceFactory::NewDevice("CPU", {}, "/job:localhost/replica:0/task:0"));
  auto ctx = new EagerContext(
      SessionOptions(),
      tensorflow::ContextDevicePlacementPolicy::DEVICE_PLACEMENT_EXPLICIT,
      false, &device_mgr, false, nullptr, nullptr);

  const Tensor kTwo = test::AsScalar<int64_t>(2);
  const string function_name = "XTimesTwo";
  const FunctionDef x_times_two = FunctionDefHelper::Define(
      // Name
      function_name,
      // Args
      {"x: int64"},
      // Return values
      {"y: int64"},
      // Attr def
      {},
      // Nodes
      {
          {{"two"}, "Const", {}, {{"value", kTwo}, {"dtype", DT_INT64}}},
          {{"scale"},
           "Cast",
           {"two"},
           {{"SrcT", DT_INT64}, {"DstT", DT_INT64}}},
          {{"y"}, "Mul", {"x", "scale"}, {{"T", DT_INT64}}},
      });
  TF_ASSERT_OK(ctx->AddFunctionDef(x_times_two));

  auto op = std::make_unique<EagerOperation>(ctx);
  TF_ASSERT_OK(op->Reset(
      /*op=*/function_name.c_str(),
      /*raw_device_name=*/"/job:localhost/replica:0/task:0/device:CPU:0"));
  TF_ASSERT_OK(op->SetAttrBool("_XlaMustCompile", true));

  Tensor input_tensor = test::AsScalar<int64_t>(3);
  auto input = core::RefCountPtr<ImmediateExecutionTensorHandle>(
      ctx->CreateLocalHandleFromTFTensor(input_tensor,
                                         ctx->HostCPUName().c_str()));
  TF_ASSERT_OK(op->AddInput(input.get()));

  monitoring::testing::CellReader<int64_t> counter_reader(
      "/tensorflow/core/tf_function_compile");
  std::vector<TensorHandle*> retvals(1);
  int num_retvals = retvals.size();
  TF_ASSERT_OK(EagerExecute(op.get(), retvals.data(), &num_retvals));
  EXPECT_EQ(counter_reader.Delta("CPU", "enabled"), 1);

  retvals[0]->Unref();
  retvals[0] = nullptr;
  ctx->Unref();
}

}  // namespace
}  // namespace tensorflow
