// Created by Leksa667
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <opencv2/opencv.hpp>
#include <memory>

cv::Mat captureScreen(int x, int y, int width, int height);

class DXGICapture {
public:
     DXGICapture();
    ~DXGICapture();

    bool    Init(int monitor_idx = 0);
    cv::Mat Capture();
    cv::Mat CaptureBgra();
    cv::Mat CaptureBgraRegion(int x, int y, int width, int height);
    int     Width()   const;
    int     Height()  const;
    bool    IsValid() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
