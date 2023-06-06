#include "sam.h"

#include <onnxruntime_cxx_api.h>

#include <codecvt>
#include <fstream>
#include <iostream>
#include <locale>
#include <opencv2/opencv.hpp>
#include <vector>

struct SamModel {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "test"};
  Ort::SessionOptions sessionOptions;
  std::unique_ptr<Ort::Session> sessionPre, sessionSam;
  std::vector<int64_t> inputShapePre, outputShapePre;
  Ort::MemoryInfo memoryInfo{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};
  bool bModelLoaded = false;
  std::vector<float> outputTensorValuesPre;

  const char *inputNamesSam[6]{"image_embeddings", "point_coords",   "point_labels",
                               "mask_input",       "has_mask_input", "orig_im_size"},
      *outputNamesSam[3]{"masks", "iou_predictions", "low_res_masks"};

  SamModel(const std::string& preModelPath, const std::string& samModelPath, int threadsNumber) {
    for (auto& p : {samModelPath, samModelPath}) {
      std::ifstream f(p);
      if (!f.good()) {
        std::cerr << "Model file " << p << " not found" << std::endl;
        return;
      }
    }

    sessionOptions.SetIntraOpNumThreads(threadsNumber);
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

#if _MSC_VER
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    auto wpreModelPath = converter.from_bytes(preModelPath);
    auto wsamModelPath = converter.from_bytes(samModelPath);
#else
    auto wpreModelPath = preModelPath;
    auto wsamModelPath = samModelPath;
#endif

    sessionPre = std::make_unique<Ort::Session>(env, wpreModelPath.c_str(), sessionOptions);
    if (sessionPre->GetInputCount() != 1 || sessionPre->GetOutputCount() != 1) {
      std::cerr << "Preprocessing model not loaded (invalid input/output count)" << std::endl;
      return;
    }

    sessionSam = std::make_unique<Ort::Session>(env, wsamModelPath.c_str(), sessionOptions);
    if (sessionSam->GetInputCount() != 6 || sessionSam->GetOutputCount() != 3) {
      std::cerr << "Model not loaded (invalid input/output count)" << std::endl;
      return;
    }

    inputShapePre = sessionPre->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    outputShapePre = sessionPre->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (inputShapePre.size() != 4 || outputShapePre.size() != 4) {
      std::cerr << "Preprocessing model not loaded (invalid shape)" << std::endl;
      return;
    }

    bModelLoaded = true;
  }

  cv::Size getInputSize() const {
    if (!bModelLoaded) return cv::Size(0, 0);
    return cv::Size(inputShapePre[3], inputShapePre[2]);
  }
  bool loadImage(const cv::Mat& image) {
    std::vector<uint8_t> inputTensorValues(inputShapePre[0] * inputShapePre[1] * inputShapePre[2] *
                                           inputShapePre[3]);

    if (image.size() != cv::Size(inputShapePre[3], inputShapePre[2])) {
      std::cerr << "Image size not match" << std::endl;
      return false;
    }
    if (image.channels() != 3) {
      std::cerr << "Input is not a 3-channel image" << std::endl;
      return false;
    }

    for (int i = 0; i < inputShapePre[2]; i++) {
      for (int j = 0; j < inputShapePre[3]; j++) {
        inputTensorValues[i * inputShapePre[3] + j] = image.at<cv::Vec3b>(i, j)[2];
        inputTensorValues[inputShapePre[2] * inputShapePre[3] + i * inputShapePre[3] + j] =
            image.at<cv::Vec3b>(i, j)[1];
        inputTensorValues[2 * inputShapePre[2] * inputShapePre[3] + i * inputShapePre[3] + j] =
            image.at<cv::Vec3b>(i, j)[0];
      }
    }

    auto inputTensor = Ort::Value::CreateTensor<uint8_t>(
        memoryInfo, inputTensorValues.data(), inputTensorValues.size(), inputShapePre.data(),
        inputShapePre.size());

    outputTensorValuesPre = std::vector<float>(outputShapePre[0] * outputShapePre[1] *
                                               outputShapePre[2] * outputShapePre[3]);
    auto outputTensorPre = Ort::Value::CreateTensor<float>(
        memoryInfo, outputTensorValuesPre.data(), outputTensorValuesPre.size(),
        outputShapePre.data(), outputShapePre.size());

    const char *inputNamesPre[] = {"input"}, *outputNamesPre[] = {"output"};

    Ort::RunOptions run_options;
    sessionPre->Run(run_options, inputNamesPre, &inputTensor, 1, outputNamesPre, &outputTensorPre,
                    1);
    return true;
  }

  void getMask(const cv::Point& point, cv::Mat& outputMaskSam, double& iouValue) const {
    const size_t maskInputSize = 256 * 256;
    float inputPointValues[] = {(float)point.x, (float)point.y}, inputLabelValues[] = {1},
          maskInputValues[maskInputSize], hasMaskValues[] = {0},
          orig_im_size_values[] = {(float)inputShapePre[2], (float)inputShapePre[3]};
    memset(maskInputValues, 0, sizeof(maskInputValues));

    int numPoints = 1;
    std::vector<int64_t> inputPointShape = {1, numPoints, 2}, pointLabelsShape = {1, numPoints},
                         maskInputShape = {1, 1, 256, 256}, hasMaskInputShape = {1},
                         origImSizeShape = {2};

    std::vector<Ort::Value> inputTensorsSam;
    inputTensorsSam.push_back(Ort::Value::CreateTensor<float>(
        memoryInfo, (float*)outputTensorValuesPre.data(), outputTensorValuesPre.size(),
        outputShapePre.data(), outputShapePre.size()));
    inputTensorsSam.push_back(Ort::Value::CreateTensor<float>(
        memoryInfo, inputPointValues, 2, inputPointShape.data(), inputPointShape.size()));
    inputTensorsSam.push_back(Ort::Value::CreateTensor<float>(
        memoryInfo, inputLabelValues, 1, pointLabelsShape.data(), pointLabelsShape.size()));
    inputTensorsSam.push_back(Ort::Value::CreateTensor<float>(
        memoryInfo, maskInputValues, maskInputSize, maskInputShape.data(), maskInputShape.size()));
    inputTensorsSam.push_back(Ort::Value::CreateTensor<float>(
        memoryInfo, hasMaskValues, 1, hasMaskInputShape.data(), hasMaskInputShape.size()));
    inputTensorsSam.push_back(Ort::Value::CreateTensor<float>(
        memoryInfo, orig_im_size_values, 2, origImSizeShape.data(), origImSizeShape.size()));

    Ort::RunOptions runOptionsSam;
    auto outputTensorsSam = sessionSam->Run(runOptionsSam, inputNamesSam, inputTensorsSam.data(),
                                            inputTensorsSam.size(), outputNamesSam, 3);

    auto outputMasksValues = outputTensorsSam[0].GetTensorMutableData<float>();
    if (outputMaskSam.type() != CV_8UC1 ||
        outputMaskSam.size() != cv::Size(inputShapePre[3], inputShapePre[2])) {
      outputMaskSam = cv::Mat(inputShapePre[2], inputShapePre[3], CV_8UC1);
    }

    for (int i = 0; i < outputMaskSam.rows; i++) {
      for (int j = 0; j < outputMaskSam.cols; j++) {
        outputMaskSam.at<uchar>(i, j) = outputMasksValues[i * outputMaskSam.cols + j] > 0 ? 255 : 0;
      }
    }

    iouValue = outputTensorsSam[1].GetTensorMutableData<float>()[0];
  }
};

Sam::Sam(const std::string& preModelPath, const std::string& samModelPath, int threadsNumber)
    : m_model(new SamModel(preModelPath, samModelPath, threadsNumber)) {}
Sam::~Sam() { delete m_model; }

cv::Size Sam::getInputSize() const { return m_model->getInputSize(); }
bool Sam::loadImage(const cv::Mat& image) { return m_model->loadImage(image); }

cv::Mat Sam::getMask(const cv::Point& point, double* iou) const {
  double iouValue = 0;
  cv::Mat m;
  m_model->getMask(point, m, iouValue);
  if (iou != nullptr) {
    *iou = iouValue;
  }
  return m;
}

// Just a poor version of https://github.com/facebookresearch/segment-anything/blob/main/notebooks/automatic_mask_generator_example.ipynb
cv::Mat Sam::autoSegment(const cv::Size& numPoints, cbProgress cb, const double iouThreshold,
                         const double minArea, int* numObjects) const {
  if (numPoints.empty()) {
    return {};
  }

  const auto size = getInputSize();
  cv::Mat mask, outImage = cv::Mat::zeros(size, CV_64FC1);

  std::vector<double> masksAreas;

  for (int i = 0; i < numPoints.height; i++) {
    for (int j = 0; j < numPoints.width; j++) {
      if (cb) {
        cb(double(i * numPoints.width + j) / (numPoints.width * numPoints.height));
      }

      cv::Point input(cv::Point((j + 0.5) * size.width / numPoints.width,
                                (i + 0.5) * size.height / numPoints.height));

      double iou;
      m_model->getMask(input, mask, iou);
      if (mask.empty() || iou < iouThreshold) {
        continue;
      }

      std::vector<std::vector<cv::Point>> contours;
      cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
      if (contours.empty()) {
        continue;
      }

      int maxContourIndex = 0;
      double maxContourArea = 0;
      for (int i = 0; i < contours.size(); i++) {
        double area = cv::contourArea(contours[i]);
        if (area > maxContourArea) {
          maxContourArea = area;
          maxContourIndex = i;
        }
      }
      if (maxContourArea < minArea) {
        continue;
      }

      cv::Mat contourMask = cv::Mat::zeros(size, CV_8UC1);
      cv::drawContours(contourMask, contours, maxContourIndex, cv::Scalar(255), cv::FILLED);
      cv::Rect boundingBox = cv::boundingRect(contours[maxContourIndex]);

      int index = masksAreas.size() + 1, numPixels = 0;
      for (int i = boundingBox.y; i < boundingBox.y + boundingBox.height; i++) {
        for (int j = boundingBox.x; j < boundingBox.x + boundingBox.width; j++) {
          if (contourMask.at<uchar>(i, j) == 0) {
            continue;
          }

          auto dst = (int)outImage.at<double>(i, j);
          if (dst > 0 && masksAreas[dst - 1] < maxContourArea) {
            continue;
          }
          outImage.at<double>(i, j) = index;
          numPixels++;
        }
      }
      if (numPixels == 0) {
        continue;
      }

      masksAreas.emplace_back(maxContourArea);
    }
  }
  return outImage;
}
