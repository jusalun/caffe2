/**
 * Copyright (c) 2016-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CAFFE2_OPERATORS_PACK_RNN_SEQUENCE_OP_H_
#define CAFFE2_OPERATORS_PACK_RNN_SEQUENCE_OP_H_

#include <algorithm>
#include <vector>
#include "caffe2/core/context.h"
#include "caffe2/core/operator.h"
#include "caffe2/utils/math.h"

namespace caffe2 {

template <class Context, bool Forward>
class PackRNNSequenceOpBase : public Operator<Context> {
 public:
  USE_OPERATOR_CONTEXT_FUNCTIONS;
  PackRNNSequenceOpBase(const OperatorDef& operator_def, Workspace* ws)
      : Operator<Context>(operator_def, ws) {}

  bool RunOnDevice() override {
    return DispatchHelper<TensorTypes<int32_t, int64_t, float, double>>::call(
        this, Input(0));
  }

  template <typename ValT>
  bool DoRunWithType() {
    // The value is copied from the sequence to the pack
    // if Forward is true, and vice versa
    int dim_offset = Forward ? 1 : 2;
    auto& values = Input(0);
    CAFFE_ENFORCE_GT(values.ndim(), dim_offset);

    // block_size is the size for each individual feature
    TIndex block_size = values.size_from_dim(dim_offset);
    auto values_vec = values.template data<ValT>();

    auto& lengths = Input(LENGTHS);
    CAFFE_ENFORCE_EQ(lengths.ndim(), 1);
    const auto cols = lengths.size();
    const int32_t* lengths_vec = lengths.template data<int32_t>();
    // the total number of rows is defined as the max number from lengths
    // if when the lengths is empty, we set rows = 0 to support zero lengths
    const auto rows =
        cols ? *std::max_element(lengths_vec, lengths_vec + cols) : 0;
    CAFFE_ENFORCE_GE(rows, 0);
    int length_sum = 0;
    if (cols > 0) {
      math::Sum<int, Context>(cols, lengths_vec, &length_sum, &context_);
    }

    vector<TIndex> shape;
    // the output shape is rows * cols for the pack,
    // or length_sum for the sequence
    if (Forward) {
      shape.push_back(rows);
      shape.push_back(cols);
    } else {
      shape.push_back(length_sum);
    }
    // insert the dim for the feature
    shape.insert(
        shape.end(), values.dims().begin() + dim_offset, values.dims().end());

    auto* output = Output(OUTPUTVALUE);
    output->Resize(shape);

    auto output_data = output->template mutable_data<ValT>();
    // initialize output_data with zero, as it is the default value for padding
    // when certain length is smaller than rows
    math::Set<ValT, Context>(output->size(), 0, output_data, &context_);

    int32_t offset = 0;
    for (int c = 0; c < cols; c++) {
      for (int r = 0; r < lengths_vec[c]; r++) {
        auto input_offset = Forward ? (offset + r) : (r * cols + c);
        auto output_offset = Forward ? (r * cols + c) : (offset + r);
        context_.template CopyItems<Context, Context>(
            values.meta(),
            block_size,
            values_vec + input_offset * block_size,
            output_data + output_offset * block_size);
      }
      offset += lengths_vec[c];
    }
    return true;
  }

 private:
  INPUT_TAGS(INPUTVALUE, LENGTHS);
  OUTPUT_TAGS(OUTPUTVALUE);
};
} // namespace caffe2

#endif // CAFFE2_OPERATORS_PACK_RNN_SEQUENCE_OP_H_
