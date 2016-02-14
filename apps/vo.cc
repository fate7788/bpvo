#include "utils/data_loader.h"
#include "utils/bounded_buffer.h"
#include "utils/program_options.h"
#include "utils/viz.h"

#include "bpvo/vo.h"
#include "bpvo/debug.h"
#include "bpvo/timer.h"
#include "bpvo/trajectory.h"
#include "bpvo/config.h"
#include "bpvo/utils.h"

#include <iostream>
#include <fstream>

#include <opencv2/highgui/highgui.hpp>

using namespace bpvo;

int main(int argc, char** argv)
{
  fprintf(stdout, "%s\n", BPVO_BUILD_STR);

  ProgramOptions options;
  options
      ("config,c", "../conf/tsukuba.cfg", "config file")
      ("output,o", "output.txt", "trajectory output file")
      ("numframes,n", int(100), "number of frames to process")
      ("dontshow,x", "do not show images")
      .parse(argc, argv);

  auto max_frames = options.get<int>("numframes");
  auto conf_fn = options.get<std::string>("config");
  auto do_show = !options.hasOption("dontshow");

  auto data_loader = DataLoader::FromConfig(conf_fn);
  typename DataLoaderThread::BufferType image_buffer(1);

  AlgorithmParameters params(conf_fn);
  std::cout << "------- AlgorithmParameters -------" << std::endl;
  std::cout << params << std::endl;
  std::cout << "-----------------------------------" << std::endl;

  auto vo = VisualOdometry(data_loader.get(), params);

  Trajectory trajectory;
  typename DataLoaderThread::BufferType::value_type frame;

  int f_i = data_loader->firstFrameNumber();
  std::cout << data_loader->calibration() << std::endl;
  DataLoaderThread data_loader_thread(std::move(data_loader), image_buffer);

  max_frames = f_i + max_frames;

  double total_time = 0.0;

  while(f_i < max_frames) {
    if(image_buffer.pop(&frame)) {
      if(frame->image().empty()) {
        Warn("could not get data\n");
        break;
      }
      Timer timer;
      auto result = vo.addFrame(frame->image().ptr<uint8_t>(),
                                frame->disparity().ptr<float>());
      double tt = timer.stop().count();
      total_time += (tt / 1000.0);
      f_i += 1;
      trajectory.push_back(result.pose);

      int num_iters = result.optimizerStatistics.front().numIterations;
      if(num_iters == params.maxIterations)
        Warn("maximum iterations reached\n");

      fprintf(stdout, "Frame %05d time %0.2f ms [%0.2f Hz] %03d iters isKeyFrame:%1d because:%16s num_points %06d\r",
              f_i-1, tt, (f_i - 1) / total_time,  num_iters, result.isKeyFrame,
              ToString(result.keyFramingReason).c_str(), vo.numPointsAtLevel(0));
      fflush(stdout);

      if(do_show) {
        cv::imshow("image", overlayDisparity(frame.get(), 0.75f));
        int k = 0xff & cv::waitKey(2);
        if(k == ' ') k = cv::waitKey(0);
        if(k == 'q' || k == 27)
          break;
      }
    }
  }

  fprintf(stdout, "\n");
  Info("Processed %d frames @ %0.2f Hz\n", f_i, f_i / total_time);

  {
    auto output_fn = options.get<std::string>("output");
    if(!output_fn.empty()) {
      Info("Writing trajectory to %s\n", output_fn.c_str());
      if(!trajectory.writeCameraPath(output_fn)) {
        Warn("failed to write trajectory to %s\n", output_fn.c_str());
      }
    }
  }

  data_loader_thread.stop();
  while(data_loader_thread.isRunning())
    Sleep(10);

  Info("done\n");

  return 0;
}
