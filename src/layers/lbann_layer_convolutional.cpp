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
//
// lbann_layer_convolutional .hpp .cpp - Convolutional Layer
////////////////////////////////////////////////////////////////////////////////

#include "lbann/layers/lbann_layer_convolutional.hpp"

using namespace std;
using namespace El;
using namespace lbann;

convolutional_layer::convolutional_layer(const uint index,
                                         const int num_dims,
                                         const int num_input_channels,
                                         const int* input_dims,
                                         const int num_output_channels,
                                         const int* filter_dims,
                                         const int* conv_pads,
                                         const int* conv_strides,
                                         const uint mini_batch_size,
                                         const activation_type activation,
                                         lbann_comm* comm,
                                         Optimizer* optimizer,
                                         std::vector<regularizer*> regs,
                                         cudnn::cudnn_manager* cudnn)
  : Layer(index, comm, optimizer, mini_batch_size, activation, regs),
    m_num_dims(num_dims), m_num_input_channels(num_input_channels),
    m_num_output_channels(num_output_channels)
{

  // Initialize input dimensions and convolution parameters
  m_input_dims.resize(num_dims);
  m_filter_dims.resize(num_dims);
  m_filter_size = num_input_channels*num_output_channels;
  m_conv_pads.resize(num_dims);
  m_conv_strides.resize(num_dims);
  for(int i=0; i<num_dims; ++i) {
    m_input_dims[i] = input_dims[i];
    m_filter_dims[i] = filter_dims[i];
    m_filter_size *= filter_dims[i];
    m_conv_pads[i] = conv_pads[i];
    m_conv_strides[i] = conv_strides[i];
  }

  // Calculate output dimensions
  m_output_dims.resize(num_dims);
  NumNeurons = num_output_channels;
  for(int i=0; i<num_dims; ++i) {
    m_output_dims[i] = input_dims[i]+2*conv_pads[i]-filter_dims[i]+1;
    m_output_dims[i] = (m_output_dims[i]+conv_strides[i]-1)/conv_strides[i];
    NumNeurons *= m_output_dims[i];
  }
  
  // Matrices should be in Star,Star and Star,VC distributions
  delete WB;
  delete WB_D;
  delete Zs;
  delete Ds;
  delete Ds_Temp;
  delete Acts;
  WB = new StarMat(comm->get_model_grid());
  WB_D = new StarMat(comm->get_model_grid());
  Zs = new StarVCMat(comm->get_model_grid());
  Ds = new StarVCMat(comm->get_model_grid());
  Ds_Temp = new StarVCMat(comm->get_model_grid());
  Acts = new StarVCMat(comm->get_model_grid());

  // Initialize cuDNN convolutional layer
  m_cudnn_layer = NULL;
#ifdef __LIB_CUDNN
  if(cudnn)
    m_cudnn_layer
      = new cudnn::cudnn_convolutional_layer(num_dims,
                                             num_input_channels,
                                             num_output_channels,
                                             input_dims,
                                             filter_dims,
                                             conv_pads,
                                             conv_strides,
                                             cudnn);
#endif // __LIB_CUDNN

}

convolutional_layer::~convolutional_layer()
{
#ifdef __LIB_CUDNN
  delete m_cudnn_layer;
#endif // __LIB_CUDNN
}

void convolutional_layer::setup(const int num_prev_neurons)
{
  Layer::setup(num_prev_neurons);

#ifdef __LIB_CUDNN
  if(m_cudnn_layer) {
    // Setup cuDNN convolutional layer
    m_cudnn_layer->setup();

    // Get output dimensions
    NumNeurons = m_cudnn_layer->m_dst_size;
    for(int i=0; i<m_num_dims; ++i)
      m_output_dims[i] = m_cudnn_layer->m_dst_dims[i+2];
  }
#endif // __LIB_CUDNN

  // Check if input dimensions are valid
  int num_inputs = m_num_input_channels;
  for(int i=0; i<m_num_dims; ++i)
    num_inputs *= m_input_dims[i];
  if(num_inputs != num_prev_neurons) {
    std::cerr << "Error: convolutional layer input dimensions don't match number of input neurons\n";
    exit(EXIT_FAILURE);
  }

  // Initialize optimizer
  if(optimizer)
    optimizer->setup(1, m_filter_size+NumNeurons);

  // Initialize filter with Xavier initialization
  DataType var_scale = sqrt(3.0 / m_filter_size);
  Gaussian(*WB, m_filter_size+NumNeurons, 1, (DataType)0.0, var_scale);
  
  // Initialize matrices
  Zeros(*WB_D, m_filter_size+NumNeurons, 1);
  Ones(*Zs, NumNeurons+1, m_mini_batch_size);
  Zeros(*Ds, NumNeurons+1, m_mini_batch_size);
  Zeros(*Ds_Temp, num_prev_neurons+1, m_mini_batch_size);
  Ones(*Acts, NumNeurons+1, m_mini_batch_size);

}

void lbann::convolutional_layer::fp_linearity(ElMat& _WB,
                                              ElMat& _X,
                                              ElMat& _Z,
                                              ElMat& _Y) {
  
  // Convert matrices to desired formats
  DistMatrixReadProxy<DataType,DataType,STAR,STAR> WBProxy(_WB);
  DistMatrixReadProxy<DataType,DataType,STAR,VC> XProxy(_X);
  DistMatrixWriteProxy<DataType,DataType,STAR,VC> ZProxy(_Z);
  DistMatrixWriteProxy<DataType,DataType,STAR,VC> YProxy(_Y);
  StarMat& WB = WBProxy.Get();
  StarVCMat& X = XProxy.Get();
  StarVCMat& Z = ZProxy.Get();
  StarVCMat& Y = YProxy.Get();

  // Get local matrices
  const Mat& WBLocal = WB.LockedMatrix();
  const Mat& XLocal = X.LockedMatrix();
  Mat& ZLocal = Z.Matrix();
  Mat& YLocal = Y.Matrix();

  // Apply convolution on local data samples
  if(m_cudnn_layer) {
#ifdef __LIB_CUDNN
    m_cudnn_layer->forward(XLocal,
                           WBLocal(IR(0,m_filter_size),ALL),
                           WBLocal(IR(m_filter_size,END),ALL),
                           ZLocal);
#else
    std::cerr << "Error: cuDNN not detected\n";
    exit(EXIT_FAILURE);
#endif
  }
  else {
    // TODO: implement convolution on CPU
    std::cerr << "Error: convolution forward pass not implemented on CPU\n";
    exit(EXIT_FAILURE);
  }

  // Z and Y are identical after fp linearity step
  Copy(ZLocal, YLocal);

}

void lbann::convolutional_layer::bp_linearity() {

  // Convert matrices to desired formats
  DistMatrixReadProxy<DataType,DataType,STAR,VC> InputProxy(*fp_input); // TODO: store from fp step
  StarVCMat& Input = InputProxy.Get();

  // Get local matrices
  const Mat& InputLocal = Input.LockedMatrix();
  const Mat& FilterLocal = WB->LockedMatrix()(IR(0,m_filter_size),ALL);
  const Mat& OutputDeltaLocal = Ds->LockedMatrix();
  Mat FilterDeltaLocal = WB_D->Matrix()(IR(0,m_filter_size),ALL);
  Mat BiasDeltaLocal = WB_D->Matrix()(IR(m_filter_size,END),ALL);
  Mat& InputDeltaLocal = Ds_Temp->Matrix();

  // Compute gradients on local data samples
  if(m_cudnn_layer) {
#ifdef __LIB_CUDNN
    m_cudnn_layer->backward(InputLocal,
                            FilterLocal,
                            OutputDeltaLocal,
                            FilterDeltaLocal,
                            BiasDeltaLocal,
                            InputDeltaLocal);
#else
    std::cerr << "Error: cuDNN not detected\n";
    exit(EXIT_FAILURE);
#endif
  }
  else {
    // TODO: implement backward pass on CPU
    std::cerr << "Error: convolution backward pass not implemented on CPU\n";
    exit(EXIT_FAILURE);
  }

  // Obtain filter gradient with reduction and scaling
  AllReduce(*WB_D, mpi::COMM_WORLD);  
  *WB_D *= 1.0/get_effective_minibatch_size();

}

bool convolutional_layer::update()
{
  optimizer->update_weight_bias_matrix(*WB_D, *WB);
  return true;
}

