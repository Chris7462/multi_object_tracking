#pragma once

#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include <set>
#include <unordered_map>

#include <opencv2/opencv.hpp>
#include <opencv2/core/eigen.hpp>

#include "multi_object_tracking/imm_ukf.hpp"
#include "multi_object_tracking/hungarianAlg.hpp"
#include "multi_object_tracking/read_param.hpp"
#include "multi_object_tracking/track.hpp"

//TODO define the form of detection
typedef std::shared_ptr<Track> track_ptr;
typedef std::vector<Detect> Detection;

class Tracker
{
  public:
    typedef std::vector<Eigen::MatrixXd> Matrices;
    typedef std::vector<Eigen::Vector2f> Vec2f;

    Tracker(Param& p)
      : param_(p)
    {
      init_ = false;
      start_tracking_ = false;
      track_id_ = 0;
      filewrite.open("./result.txt", std::ios::out | std::ios::app);

      chi2in975[1] = 5.025;
      chi2in975[2] = 7.378;
      chi2in975[3] = 9.348;
      chi2in975[4] = 11.143;
      chi2in975[5] = 12.833;
      chi2in975[6] = 14.449;
    };

    ~Tracker(){};

    void Init(const Detection& detections, float& time);

    void manage_tracks(float& time);

    void associate(Detection& _selected_detections, cv::Mat& _q, const Detection& _detections);
    Matrices generate_hypothesis(const cv::Mat& _q);

    std::vector<bool> analyze_tracks(const cv::Mat& _q);

    Eigen::MatrixXd joint_probability(const Matrices& _association_matrices, const Detection& selected_detections);
    Eigen::MatrixXd joint_probability(const Matrices& _association_matrices, const Detection& selected_detections, std::vector<track_ptr>& tracks_in);

    void track(const Detection& detection, float& time, std::vector<Eigen::VectorXd>& result);

    void delete_tracks();

    void pruning(Detection&  selected_detections, std::vector<int>& final_select,std::vector<track_ptr>& tacks_in);

  private:
    constexpr static uint MAX_ASSOC = 10000;

    std::vector<track_ptr> tracks_;
    std::vector<track_ptr> unconfirmed_tracks_;
    std::vector<track_ptr> confirmed_tracks_;

    int track_id_;

    bool init_;
    bool start_tracking_;

    float pretime;

    Detection not_associated_;
    Detection prev_detections_;

    Param param_;
    TrackState Track_state_;

    std::ofstream filewrite;

    std::unordered_map<int, float> chi2in975;
};