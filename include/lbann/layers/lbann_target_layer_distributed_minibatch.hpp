////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014-2016, Lawrence Livermore National Security, LLC. 
// Produced at the Lawrence Livermore National Laboratory. 
// Written by the LBANN Research Team (B. Van Essen, et al.) listed in
// the CONTRIBUTORS file. <lbann-dev@llnl.gov>
//
// LLNL-CODE-697807.
// All rights reserved.
//
// This file is part of LBANN: Livermore Big Artificial Neural Network
// Toolkit. For details, see http://software.llnl.gov/LBANN or
// https://github.com/LLNL/LBANN. 
//
// Licensed under the Apache License, Version 2.0 (the "Licensee"); you
// may not use this file except in compliance with the License.  You may
// obtain a copy of the License at:
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the license.
////////////////////////////////////////////////////////////////////////////////

#ifndef LBANN_LAYERS_TARGET_LAYER_DISTRIBUTED_MINIBATCH_HPP_INCLUDED
#define LBANN_LAYERS_TARGET_LAYER_DISTRIBUTED_MINIBATCH_HPP_INCLUDED

#include "lbann/layers/lbann_target_layer.hpp"

namespace lbann
{
  class target_layer_distributed_minibatch : public target_layer {
  public:
    target_layer_distributed_minibatch(lbann_comm* comm, uint mini_batch_size, DataReader* training_data_reader, DataReader *testing_data_reader, bool shared_data_reader);
    target_layer_distributed_minibatch(lbann_comm* comm, uint mini_batch_size, DataReader* training_data_reader, bool shared_data_reader);

    void setup(int num_prev_neurons);
    // Overrides regular forward prop; target layer doesn't involve sparsity, etc.
    DataType forwardProp(DataType prev_WBL2NormSum);
    void backProp();
    bool update();

  public:
    int m_root; /* Which rank is the root of the CircMat */
    Mat Y_local;
    CircMat Ys;
  protected:
    void fp_linearity(ElMat&, ElMat&, ElMat&, ElMat&) {}
    void bp_linearity() {}
  };
}

#endif  // LBANN_LAYERS_TARGET_LAYER_DISTRIBUTED_MINIBATCH_HPP_INCLUDED
