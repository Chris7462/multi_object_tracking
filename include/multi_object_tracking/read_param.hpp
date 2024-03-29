#pragma once

#include <fstream>
#include <string>
#include <iostream>
#include <cstdlib>
#include <Eigen/Dense>
#include <Eigen/Geometry>


class Param
{
  public:
    Param() {
      int modelnum = 3;
      pP = Eigen::MatrixXd(6,6);
      pP.fill(0.0);
      for (int i=0; i<6; ++i) {
        pP(i,i) = 1.;
      }

      pmodel = std::vector<std::string>(modelnum);
      pmodel[0] = "CTRV";
      pmodel[1] = "CTRA";
      pmodel[2] = "CV";

      pQ = std::vector<Eigen::MatrixXd>(modelnum);
      for (int k = 0; k < modelnum; ++k) {
        pQ[k] = Eigen::MatrixXd(6,6);
        pQ[k].fill(0.0);
        for(int i = 0; i < 6; ++i) {
          pQ[k](i,i) = 1e-25;
        }
        pQ[k](1,1) = 4e-2;
        pQ[k](2,2) = 4e-3;
        pQ[k](3,3) = 4e-3;
        pQ[k](4,4) = 1e-15;
      }

      pR = std::vector<Eigen::MatrixXd>(modelnum);
      for (int k = 0; k < modelnum; ++k) {
        pR[k] = Eigen::MatrixXd(2,2);
        pR[k].fill(0.0);
        pR[k](0,0) = 1e-3;
        pR[k](1,1) = 1e-3;
      }

      pinteract = Eigen::MatrixXd(modelnum,modelnum);
      pinteract.fill(0.05);
      pinteract(0,0) = 0.9;
      pinteract(1,1) = 0.9;
      pinteract(2,2) = 0.9;
      //pinteract(2,0) = 0.15;
      //pinteract(2,1) = 0.15;
    };

    Param(const Param& pcopy) {
      pstate_v = pcopy.pstate_v;
      pmea_v = pcopy.pmea_v;
      pIou_thresh = pcopy.pIou_thresh;
      pg_sigma = pcopy.pg_sigma;
      pdist_thresh = pcopy.pdist_thresh;
      pmodel = pcopy.pmodel;
      pP = pcopy.pP;
      pinteract = pcopy.pinteract;
      pQ = pcopy.pQ;
      pR = pcopy.pR;
      pd = pcopy.pd;
    }

    Param& operator=(const Param& pcopy) {
      if (this == &pcopy) {
        return *this;
      } else {
        pstate_v = pcopy.pstate_v;
        pmea_v = pcopy.pmea_v;
        pIou_thresh = pcopy.pIou_thresh;
        pg_sigma = pcopy.pg_sigma;
        pdist_thresh = pcopy.pdist_thresh;
        pmodel = pcopy.pmodel;
        pP = pcopy.pP;
        pinteract = pcopy.pinteract;
        pQ = pcopy.pQ;
        pR = pcopy.pR;
        pd = pcopy.pd;
        return *this;
      }
    }

    float pd = 1.0;

    int pstate_v = 6;
    int pmea_v = 2;

    float pIou_thresh = 0.1;
    float pg_sigma = 15; //for the Mahalanobis distdance threshold: pi*g*|s|
    float pdist_thresh = 1.5;

    float pi = 3.1415926;

    std::vector<std::string> pmodel;

    Eigen::MatrixXd pP;
    Eigen::MatrixXd pinteract;

    std::vector<Eigen::MatrixXd> pQ;
    std::vector<Eigen::MatrixXd> pR;
};
