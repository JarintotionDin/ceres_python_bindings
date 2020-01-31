#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <ceres/ceres.h>
#include "ceres/ceres.h"
#include "ceres/rotation.h"

namespace py = pybind11;

struct HelloWorldCostFunctor {
  template<typename T>
  bool operator()(const T *const x, T *residual) const {
    residual[0] = T(10.0) - x[0];
    return true;
  }
};

ceres::CostFunction *CreateHelloWorldCostFunction() {
  return new ceres::AutoDiffCostFunction<HelloWorldCostFunctor,
                                         1,
                                         1>(new HelloWorldCostFunctor);
}

// Read a Bundle Adjustment in the Large dataset.
class BALProblem {
 public:
  ~BALProblem() {
    delete[] point_index_;
    delete[] camera_index_;
    delete[] observations_;
    delete[] parameters_;
  }
  int num_observations() const { return num_observations_; }
  const double *observations() const { return observations_; }
  double *mutable_cameras() { return parameters_; }
  double *mutable_points() { return parameters_ + 9 * num_cameras_; }
  double *mutable_camera_for_observation(int i) {
    return mutable_cameras() + camera_index_[i] * 9;
  }
  double *mutable_point_for_observation(int i) {
    return mutable_points() + point_index_[i] * 3;
  }
  bool LoadFile(const char *filename) {
    FILE *fptr = fopen(filename, "r");
    if (fptr == NULL) {
      return false;
    };
    FscanfOrDie(fptr, "%d", &num_cameras_);
    FscanfOrDie(fptr, "%d", &num_points_);
    FscanfOrDie(fptr, "%d", &num_observations_);
    point_index_ = new int[num_observations_];
    camera_index_ = new int[num_observations_];
    observations_ = new double[2 * num_observations_];
    num_parameters_ = 9 * num_cameras_ + 3 * num_points_;
    parameters_ = new double[num_parameters_];
    for (int i = 0; i < num_observations_; ++i) {
      FscanfOrDie(fptr, "%d", camera_index_ + i);
      FscanfOrDie(fptr, "%d", point_index_ + i);
      for (int j = 0; j < 2; ++j) {
        FscanfOrDie(fptr, "%lf", observations_ + 2 * i + j);
      }
    }
    for (int i = 0; i < num_parameters_; ++i) {
      FscanfOrDie(fptr, "%lf", parameters_ + i);
    }
    return true;
  }

  template<typename T>
  void FscanfOrDie(FILE *fptr, const char *format, T *value) {
    int num_scanned = fscanf(fptr, format, value);
    if (num_scanned != 1) {
      LOG(FATAL) << "Invalid UW data file.";
    }
  }
  int num_cameras_;
  int num_points_;
  int num_observations_;
  int num_parameters_;
  int *point_index_;
  int *camera_index_;
  double *observations_;
  double *parameters_;
};

// Templated pinhole camera model for used with Ceres.  The camera is
// parameterized using 9 parameters: 3 for rotation, 3 for translation, 1 for
// focal length and 2 for radial distortion. The principal point is not modeled
// (i.e. it is assumed be located at the image center).
struct SnavelyReprojectionError {
  SnavelyReprojectionError(double observed_x, double observed_y)
      : observed_x(observed_x), observed_y(observed_y) {}
  template<typename T>
  bool operator()(const T *const camera,
                  const T *const point,
                  T *residuals) const {
    // camera[0,1,2] are the angle-axis rotation.
    T p[3];
    ceres::AngleAxisRotatePoint(camera, point, p);
    // camera[3,4,5] are the translation.
    p[0] += camera[3];
    p[1] += camera[4];
    p[2] += camera[5];
    // Compute the center of distortion. The sign change comes from
    // the camera model that Noah Snavely's Bundler assumes, whereby
    // the camera coordinate system has a negative z axis.
    T xp = -p[0] / p[2];
    T yp = -p[1] / p[2];
    // Apply second and fourth order radial distortion.
    const T &l1 = camera[7];
    const T &l2 = camera[8];
    T r2 = xp * xp + yp * yp;
    T distortion = 1.0 + r2 * (l1 + l2 * r2);
    // Compute final projected point position.
    const T &focal = camera[6];
    T predicted_x = focal * distortion * xp;
    T predicted_y = focal * distortion * yp;
    // The error is the difference between the predicted and observed position.
    residuals[0] = predicted_x - observed_x;
    residuals[1] = predicted_y - observed_y;
    return true;
  }
  // Factory to hide the construction of the CostFunction object from
  // the client code.
  static ceres::CostFunction *Create(const double observed_x,
                                     const double observed_y) {
    return (new ceres::AutoDiffCostFunction<SnavelyReprojectionError, 2, 9, 3>(
        new SnavelyReprojectionError(observed_x, observed_y)));
  }
  double observed_x;
  double observed_y;
};

void add_pybinded_ceres_examples(py::module &m) {

  m.def("CreateHelloWorldCostFunction", &CreateHelloWorldCostFunction);

  py::class_<BALProblem> bal(m, "BALProblem");
  bal.def(py::init<>());
  bal.def("num_observations", &BALProblem::num_observations);
  bal.def("LoadFile", &BALProblem::LoadFile);

  bal.def("observations", [](BALProblem &myself) {
    std::vector<double> double_data;
    for (int idx = 0; idx < myself.num_observations(); ++idx) {
      double_data.push_back(myself.observations()[2 * idx + 0]);
      double_data.push_back(myself.observations()[2 * idx + 1]);
    }
    return double_data;
  });
  bal.def("cameras", [](BALProblem &myself) {
    std::vector<double> double_data;
    for (int idx = 0; idx < myself.num_cameras_; ++idx) {
      double_data.push_back(myself.mutable_cameras()[9 * idx + 0]);
      double_data.push_back(myself.mutable_cameras()[9 * idx + 1]);
      double_data.push_back(myself.mutable_cameras()[9 * idx + 2]);
      double_data.push_back(myself.mutable_cameras()[9 * idx + 3]);
      double_data.push_back(myself.mutable_cameras()[9 * idx + 4]);
      double_data.push_back(myself.mutable_cameras()[9 * idx + 5]);
      double_data.push_back(myself.mutable_cameras()[9 * idx + 6]);
      double_data.push_back(myself.mutable_cameras()[9 * idx + 7]);
      double_data.push_back(myself.mutable_cameras()[9 * idx + 8]);
    }
    return double_data;
  });
  bal.def("points", [](BALProblem &myself) {
    std::vector<double> double_data;
    for (int idx = 0; idx < myself.num_points_; ++idx) {
      double_data.push_back(myself.mutable_points()[9 * idx + 0]);
      double_data.push_back(myself.mutable_points()[9 * idx + 1]);
      double_data.push_back(myself.mutable_points()[9 * idx + 2]);
    }
    return double_data;
  });
  bal.def("mutable_cameras_for_observation", [](BALProblem &myself, int i) {
    std::vector<double> double_data;
    double *ptr = myself.mutable_cameras() + myself.camera_index_[i] * 9;
    for (int idx = 0; idx < 9; ++idx) {
      double_data.push_back(ptr[idx]);
    }
    return double_data;
  });
  bal.def("mutable_point_for_observation", [](BALProblem &myself, int i) {
    std::vector<double> double_data;
    double *ptr = myself.mutable_points() + myself.point_index_[i] * 3;
    for (int idx = 0; idx < 3; ++idx) {
      double_data.push_back(ptr[idx]);
    }
    return double_data;
  });

  bal.def("camera_index",[](BALProblem &myself, int i){
    return myself.camera_index_[i];
  });

  bal.def("point_index",[](BALProblem &myself, int i){
    return myself.point_index_[i];
  });

  m.def("CreateSnavelyCostFunction", &SnavelyReprojectionError::Create);
}