// g2o - General Graph Optimization
// Copyright (C) 2011 R. Kuemmerle, G. Grisetti, W. Burgard
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
// TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "gtest/gtest.h"

#include "g2o/core/jacobian_workspace.h"
#include "g2o/types/slam3d/edge_se3.h"
#include "g2o/types/slam3d/edge_pointxyz.h"
#include "g2o/types/slam3d/dquat2mat.h"

#include "EXTERNAL/ceres/autodiff.h"

using namespace std;
using namespace g2o;
using namespace Eigen;

static Eigen::Isometry3d randomIsometry3d()
{
  Eigen::Vector3d rotAxisAngle = Vector3d::Random();
  rotAxisAngle += Vector3d::Random();
  Eigen::AngleAxisd rotation(rotAxisAngle.norm(), rotAxisAngle.normalized());
  Eigen::Isometry3d result = (Eigen::Isometry3d)rotation.toRotationMatrix();
  result.translation() = Vector3d::Random();
  return result;
}

template <typename EdgeType>
void evaluateJacobian(EdgeType& e, JacobianWorkspace& jacobianWorkspace, JacobianWorkspace& numericJacobianWorkspace)
{
    // calling the analytic Jacobian but writing to the numeric workspace
    e.BaseBinaryEdge<EdgeType::Dimension, typename EdgeType::Measurement,
      typename EdgeType::VertexXiType, typename EdgeType::VertexXjType>::linearizeOplus(numericJacobianWorkspace);
    // copy result into analytic workspace
    jacobianWorkspace = numericJacobianWorkspace;

    // compute the numeric Jacobian into the numericJacobianWorkspace workspace as setup by the previous call
    e.BaseBinaryEdge<EdgeType::Dimension, typename EdgeType::Measurement,
      typename EdgeType::VertexXiType, typename EdgeType::VertexXjType>::linearizeOplus();

    // compare the two Jacobians
    for (int i = 0; i < 2; ++i) {
      double* n = numericJacobianWorkspace.workspaceForVertex(i);
      double* a = jacobianWorkspace.workspaceForVertex(i);
      int numElems = EdgeType::Dimension;
      if (i == 0)
        numElems *= EdgeType::VertexXiType::Dimension;
      else
        numElems *= EdgeType::VertexXjType::Dimension;
      for (int j = 0; j < numElems; ++j) {
        EXPECT_NEAR(n[j], a[j], 1e-6);
      }
    }
}

TEST(Slam3D, EdgeSE3Jacobian)
{
  VertexSE3 v1;
  v1.setId(0); 

  VertexSE3 v2;
  v2.setId(1); 

  EdgeSE3 e;
  e.setVertex(0, &v1);
  e.setVertex(1, &v2);
  e.setInformation(EdgeSE3::InformationType::Identity());

  JacobianWorkspace jacobianWorkspace;
  JacobianWorkspace numericJacobianWorkspace;
  numericJacobianWorkspace.updateSize(&e);
  numericJacobianWorkspace.allocate();

  for (int k = 0; k < 10000; ++k) {
    v1.setEstimate(randomIsometry3d());
    v2.setEstimate(randomIsometry3d());
    e.setMeasurement(randomIsometry3d());

    evaluateJacobian(e, jacobianWorkspace, numericJacobianWorkspace);
  }
}

TEST(Slam3D, EdgePointXYZJacobian)
{
  VertexPointXYZ v1;
  v1.setId(0); 

  VertexPointXYZ v2;
  v2.setId(1); 

  EdgePointXYZ e;
  e.setVertex(0, &v1);
  e.setVertex(1, &v2);
  e.setInformation(EdgePointXYZ::InformationType::Identity());

  JacobianWorkspace jacobianWorkspace;
  JacobianWorkspace numericJacobianWorkspace;
  numericJacobianWorkspace.updateSize(&e);
  numericJacobianWorkspace.allocate();

  for (int k = 0; k < 10000; ++k) {
    v1.setEstimate(Eigen::Vector3d::Random());
    v2.setEstimate(Eigen::Vector3d::Random());
    e.setMeasurement(Eigen::Vector3d::Random());

    evaluateJacobian(e, jacobianWorkspace, numericJacobianWorkspace);
  }
}

/**
 * \brief Functor used to compute the Jacobian via AD
 */
struct RotationMatrix2QuaternionManifold
{
  template<typename T>
  bool operator()(const T* rotMatSerialized, T* quaternion) const
  {
    typename Eigen::Matrix<T, 3, 3>::ConstMapType R(rotMatSerialized);

    T t = R.trace();
    if (t > T(0)) {
      //cerr << "w";
      t = sqrt(t + T(1));
      //T w = T(0.5)*t;
      t = T(0.5)/t;
      quaternion[0] = (R(2,1) - R(1,2)) * t;
      quaternion[1] = (R(0,2) - R(2,0)) * t;
      quaternion[2] = (R(1,0) - R(0,1)) * t;
    } else {
      int i = 0;
      if (R(1,1) > R(0,0))
        i = 1;
      if (R(2,2) > R(i,i))
        i = 2;
      int j = (i+1)%3;
      int k = (j+1)%3;
      //cerr << i;

      t = sqrt(R(i,i) - R(j,j) - R(k,k) + T(1.0));
      quaternion[i] = T(0.5) * t;
      t = T(0.5)/t;
      quaternion[j] = (R(j,i) + R(i,j)) * t;
      quaternion[k] = (R(k,i) + R(i,k)) * t;
      T w = (R(k,j) - R(j,k)) * t;
      // normalize to our manifold, such that w is positive
      if (w < 0.) {
        //cerr << "  normalizing w > 0  ";
        for (int l = 0; l < 3; ++l)
          quaternion[l] *= T(-1);
      }
    }
    return true;
  }
};

TEST(Slam3D, dqDRJacobian)
{
  for (int k = 0; k < 10000; ++k) {
    // create a random rotation matrix by sampling a random 3d vector
    // that will be used in axis-angle representation to create the matrix
    Eigen::Vector3d rotAxisAngle = Vector3d::Random();
    rotAxisAngle += Vector3d::Random();
    Eigen::AngleAxisd rotation(rotAxisAngle.norm(), rotAxisAngle.normalized());
    Eigen::Matrix3d Re = rotation.toRotationMatrix();

    // our analytic function which we want to evaluate
    Eigen::Matrix<double, 3 , 9>  dq_dR;
    g2o::internal::compute_dq_dR (dq_dR, 
        Re(0,0),Re(1,0),Re(2,0),
        Re(0,1),Re(1,1),Re(2,1),
        Re(0,2),Re(1,2),Re(2,2));

    // compute the Jacobian using AD
    Eigen::Matrix<double, 3 , 9, Eigen::RowMajor> dq_dR_AD;
    typedef ceres::internal::AutoDiff<RotationMatrix2QuaternionManifold, double, 9> AutoDiff_Dq_DR;
    double *parameters[] = { Re.data() };
    double *jacobians[] = { dq_dR_AD.data() };
    double value[3];
    RotationMatrix2QuaternionManifold rot2quat;
    AutoDiff_Dq_DR::Differentiate(rot2quat, parameters, 3, value, jacobians);

    double maxDifference = (dq_dR - dq_dR_AD).array().abs().maxCoeff();
    EXPECT_NEAR(0., maxDifference, 1e-7);
  }
}
