#include <cmath>
#include <ctime>
#include <string>

// ros header
#include <tf2/LinearMath/Quaternion.h>
#include <tf2/LinearMath/Matrix3x3.h>
#include <pcl_conversions/pcl_conversions.h>
#include <std_msgs/msg/header.hpp>

// local header
#include "multi_object_tracking/track.hpp"
#include "multi_object_tracking/multi_object_tracking.hpp"
#include "multi_object_tracking/lonlat2utm.hpp"


MultiObjectTracking::MultiObjectTracking()
  : Node("multi_object_tracking"), param{}, tracker{param},
    rotZorigin_{Eigen::Matrix3d::Zero()}, porigin_{Eigen::Isometry3d::Identity()},
    oriheading_{0.0}, orix_{0.0}, oriy_{0.0},
    rotZpre_{Eigen::Matrix3d::Zero()}, ppre_{Eigen::Isometry3d::Identity()},
    preheading_{0.0}, prex_{0.0}, prey_{0.0},
    rng_{12345}, time_{0.1f}, totaltime_{0.0}, latitude_{0.0}, longitude_{0.0}, heading_{0.0},
    image_ptr_{nullptr}, init_{false}, curr_time_{get_clock()->now()}, prev_time_{curr_time_},
    max_marker_size_{50}
{
  gps_subscriber_ = create_subscription<sensor_msgs::msg::NavSatFix>(
    "gps/fix", 10, std::bind(&MultiObjectTracking::gps_callback, this, std::placeholders::_1));

  imu_subscriber_ = create_subscription<sensor_msgs::msg::Imu>(
    "imu/data", 10, std::bind(&MultiObjectTracking::imu_callback, this, std::placeholders::_1));

  img_subscriber_ = create_subscription<sensor_msgs::msg::Image>(
    "camera/image_raw", 10, std::bind(&MultiObjectTracking::img_callback, this, std::placeholders::_1));

  detect_subscriber_ = create_subscription<tracking_msgs::msg::DetectedObjectList>(
    "detected_object_list", 10, std::bind(&MultiObjectTracking::det_callback, this, std::placeholders::_1));

  img_publisher_ = create_publisher<sensor_msgs::msg::Image>("mot/image", 10);
  marker_publisher_ = create_publisher<visualization_msgs::msg::MarkerArray>("mot/box", 10);
  textmarker_publisher_= create_publisher<visualization_msgs::msg::MarkerArray>("mot/id", 10);
};

void MultiObjectTracking::gps_callback(const sensor_msgs::msg::NavSatFix::SharedPtr gps_msg)
{
  latitude_ = gps_msg->latitude;
  longitude_ = gps_msg->longitude;
}

void MultiObjectTracking::imu_callback(const sensor_msgs::msg::Imu::SharedPtr imu_msg)
{
  tf2::Quaternion q(
    imu_msg->orientation.x,
    imu_msg->orientation.y,
    imu_msg->orientation.z,
    imu_msg->orientation.w);

  tf2::Matrix3x3 m(q);
  double roll, pitch, yaw;
  m.getRPY(roll, pitch, yaw);
  heading_ = yaw - 90 * M_PI/180;
}

void MultiObjectTracking::img_callback(const sensor_msgs::msg::Image::SharedPtr img_msg)
{
  RCLCPP_INFO(get_logger(), "In the image callback");
  try {
    image_ptr_ = cv_bridge::toCvCopy(img_msg, sensor_msgs::image_encodings::BGR8);
  } catch (cv_bridge::Exception& e) {
    RCLCPP_ERROR(get_logger(), "cv_bridge exception: %s", e.what());
    rclcpp::shutdown();
  }
}

void MultiObjectTracking::det_callback(const tracking_msgs::msg::DetectedObjectList::SharedPtr det_msg)
{
  curr_time_ = get_clock()->now();
  RCLCPP_INFO(get_logger(), "In the detection callback");
  visualization_msgs::msg::MarkerArray marker_array;
  visualization_msgs::msg::Marker bbox_marker;
  bbox_marker.header.frame_id = "lidar_link";
  bbox_marker.header.stamp = curr_time_;
  bbox_marker.ns = "";
  bbox_marker.lifetime = rclcpp::Duration(0, 5e8);
  bbox_marker.frame_locked = true;
  bbox_marker.type = visualization_msgs::msg::Marker::CUBE;
  bbox_marker.action = visualization_msgs::msg::Marker::ADD;

  visualization_msgs::msg::MarkerArray text_marker_array;
  visualization_msgs::msg::Marker text_marker;
  text_marker.header.frame_id = "lidar_link";
  text_marker.header.stamp = curr_time_;
  text_marker.ns = "";
  text_marker.lifetime = rclcpp::Duration(0, 5e8);
  text_marker.frame_locked = true;
  text_marker.color.r = 0.0f;
  text_marker.color.g = 0.0f;
  text_marker.color.b = 0.0f;
  text_marker.color.a = 0.9;
  text_marker.type = visualization_msgs::msg::Marker::TEXT_VIEW_FACING;
  text_marker.action = visualization_msgs::msg::Marker::ADD;

  double UTME;
  double UTMN;
  LonLat2UTM(longitude_, latitude_, UTME, UTMN);

  Eigen::Isometry3d translate2origin;
  Eigen::Isometry3d origin2translate;

  if (!init_) {
    orix_ = UTME;
    oriy_ = UTMN;
    oriheading_ = heading_;
    Eigen::AngleAxis roto(heading_, Eigen::Vector3d::UnitZ());
    rotZorigin_ = roto.toRotationMatrix();
    porigin_ = rotZorigin_;
    porigin_.translation() = Eigen::Vector3d(UTME, UTMN, 0);

    prex_ = UTME;
    prey_ = UTMN;
    preheading_ = heading_;
    rotZpre_ = rotZorigin_;
    ppre_ = rotZpre_;
    ppre_.translation() = Eigen::Vector3d(UTME, UTMN, 0);
    init_ = true;
  } else {
    Eigen::AngleAxisd rotnow(heading_, Eigen::Vector3d::UnitZ());
    Eigen::Matrix3d rotpnow = rotnow.toRotationMatrix();
    Eigen::Isometry3d p2;
    p2 = rotpnow;
    p2.translation() = Eigen::Vector3d(UTME, UTMN, 0);
    translate2origin = porigin_.inverse() * p2;
    origin2translate = p2.inverse() * porigin_;
  }

  std::vector<Detect> Inputdets;
  for (auto& obj: det_msg->data) {
    Eigen::Vector3d cpoint(obj.x, obj.y, obj.z);
    Eigen::Vector3d ppoint = camera2cloud(cpoint);

    Detect det;
    det.box2D.resize(4);
    det.box.resize(3);
    det.classname = obj.label;
    det.box2D[0] = obj.left;
    det.box2D[1] = obj.top;
    det.box2D[2] = obj.right;
    det.box2D[3] = obj.bottom;
    det.box[0] = obj.height;
    det.box[1] = obj.width;
    det.box[2] = obj.length;
    det.z = ppoint(2);
    det.yaw = obj.yaw;
    det.position = Eigen::VectorXd(2);
    det.position << ppoint(0), ppoint(1);

    Inputdets.push_back(det);
  }

  std::vector<int> color = {0,255,0};
  for (auto& det : Inputdets) {
    std::cout<<"input: "<< det.position(0)<<" "<<det.position(1)<<" "<<det.z<<" "<<det.box[0]<<" "<<det.box[1]<<" "<<det.box[2]<<std::endl;

    Eigen::VectorXd v(2,1);
    v(1) = det.position(0); //x in kitti lidar
    v(0) = -det.position(1); //y in kitti lidar
    if (init_) {
      Eigen::Vector3d p_0(v(0), v(1), 0);
      Eigen::Vector3d p_1;
      p_1 = translate2origin * p_0;
      v(0) = p_1(0);
      v(1) = p_1(1);
    }

    det.position(0) = v(0);
    det.position(1) = v(1);

  //det.rotbox = cv::RotatedRect(
  //  cv::Point2f((v(0)+25)*608/50, v(1)*608/50),
  //  cv::Size2f(det.box[1]*608/50, det.box[2]*608/50), det.yaw);
  }

  std::vector<Eigen::VectorXd> result;
  int64_t tm0 = gtm();
  tracker.track(Inputdets, time_, result);
  int64_t tm1 = gtm();
  RCLCPP_INFO(get_logger(), "Update cast time: %ld us\n", tm1-tm0);

  double x = tm1-tm0;
  totaltime_ += x;

  int marker_id = 0;
  for (size_t i = 0; i < result.size(); ++i) {
    Eigen::VectorXd r = result.at(i);
    if (init_) {
      Eigen::Vector3d p_0(r(1), r(2), 0);
      Eigen::Vector3d p_1;
      p_1 = origin2translate * p_0;
      r(1) = p_1(0);
      r(2) = p_1(1);
    }

    Detect det;
    det.box2D.resize(4);
    det.box.resize(3);
    det.box[0] = r(9);//h
    det.box[1] = r(8);//w
    det.box[2] = r(7);//l
    det.z = r(10);
    det.yaw = r(6);
    det.position = Eigen::VectorXd(2);
    det.position << r(2),-r(1);
    std::cout<<"det: "<<det.position(0)<<" "<<det.position(1)<<" "<<det.z<<" "
             <<det.box[0]<<" "<<det.box[1]<<" "<<det.box[2]<<std::endl;

    if (!idcolor.count(int(r(0)))){
      int red = rng_.uniform(0, 255);
      int green = rng_.uniform(0, 255);
      int blue = rng_.uniform(0, 255);
      idcolor[int(r(0))] = {red,green,blue};
    }

    Eigen::Vector3f eulerAngle(-det.yaw, 0.0, 0.0);
    Eigen::AngleAxisf rollAngle(Eigen::AngleAxisf(eulerAngle(2), Eigen::Vector3f::UnitX()));
    Eigen::AngleAxisf pitchAngle(Eigen::AngleAxisf(eulerAngle(1), Eigen::Vector3f::UnitY()));
    Eigen::AngleAxisf yawAngle(Eigen::AngleAxisf(eulerAngle(0), Eigen::Vector3f::UnitZ()));
    const Eigen::Quaternionf bboxQ1(yawAngle * pitchAngle * rollAngle);
    Eigen::Vector4f q = bboxQ1.coeffs();

    bbox_marker.id = marker_id;
    bbox_marker.pose.position.x = det.position(0);
    bbox_marker.pose.position.y = det.position(1);
    bbox_marker.pose.position.z = det.z+ det.box[0]/2;
    bbox_marker.pose.orientation.x = q[0];
    bbox_marker.pose.orientation.y = q[1];
    bbox_marker.pose.orientation.z = q[2];
    bbox_marker.pose.orientation.w = q[3];
    bbox_marker.scale.x = det.box[1];
    bbox_marker.scale.y = det.box[2];
    bbox_marker.scale.z = det.box[0];
    bbox_marker.color.r = float(idcolor[int(r(0))][0])/255;
    bbox_marker.color.g = float(idcolor[int(r(0))][1])/255;
    bbox_marker.color.b = float(idcolor[int(r(0))][2])/255;
    bbox_marker.color.a = 0.8;
    marker_array.markers.push_back(bbox_marker);

    text_marker.id = marker_id;
    text_marker.pose.position.x = det.position(0);
    text_marker.pose.position.y = det.position(1);
    text_marker.pose.position.z = det.z + det.box[0]/2 + 1;
    text_marker.pose.orientation.x = 0;
    text_marker.pose.orientation.y = 0;
    text_marker.pose.orientation.z = 0;
    text_marker.pose.orientation.w = 1;
    text_marker.scale.x = 0.2;
    text_marker.scale.y = 0;
    text_marker.scale.z = 1;
    text_marker.text = "ID: " + std::to_string(int(r(0)));
    text_marker_array.markers.push_back(text_marker);
    ++marker_id;
  }

  if (marker_array.markers.size() > max_marker_size_) {
    max_marker_size_ = marker_array.markers.size();
  }

  for (size_t i = marker_id; i < max_marker_size_; ++i) {
    bbox_marker.id = i;
    bbox_marker.color.a = 0;
    bbox_marker.pose.position.x = 0;
    bbox_marker.pose.position.y = 0;
    bbox_marker.pose.position.z = 0;
    bbox_marker.scale.x = 0;
    bbox_marker.scale.y = 0;
    bbox_marker.scale.z = 0;
    marker_array.markers.push_back(bbox_marker);

    text_marker.id = i;
    text_marker.color.a = 0;
    text_marker.pose.position.x = 0;
    text_marker.pose.position.y = 0;
    text_marker.pose.position.z = 0;
    text_marker.scale.x = 0;
    text_marker.scale.y = 0;
    text_marker.scale.z = 0;
    text_marker_array.markers.push_back(text_marker);
    ++marker_id;
  }

  marker_publisher_->publish(marker_array);
  textmarker_publisher_->publish(text_marker_array);

  RCLCPP_INFO_STREAM(get_logger(), "Curr time = " << curr_time_.nanoseconds());
  RCLCPP_INFO_STREAM(get_logger(), "Prev time = " << prev_time_.nanoseconds());

  time_ += static_cast<float>((curr_time_ - prev_time_).seconds());
  prev_time_ = curr_time_;
}

cv::Point MultiObjectTracking::cloud2camera(const Eigen::Vector3d& input)
{
  Eigen::Matrix4d RT_velo_to_cam;
  Eigen::Matrix4d R_rect;
  Eigen::MatrixXd project_matrix(3,4);

  RT_velo_to_cam <<  7.967514000000e-03, -9.999679000000e-01, -8.462264000000e-04, -1.377769000000e-02,
                    -2.771053000000e-03,  8.241710000000e-04, -9.999958000000e-01, -5.542117000000e-02,
                     9.999644000000e-01,  7.969825000000e-03, -2.764397000000e-03, -2.918589000000e-01,
                     0.0,                 0.0,                 0.0,                 1.0;

  R_rect <<  9.999454000000e-01, 7.259129000000e-03, -7.519551000000e-03, 0.0,
            -7.292213000000e-03, 9.999638000000e-01, -4.381729000000e-03, 0.0,
             7.487471000000e-03, 4.436324000000e-03,  9.999621000000e-01, 0.0,
             0.0,                0.0,                 0.0,                1.0;

  project_matrix << 7.188560000000e+02, 0.000000000000e+00, 6.071928000000e+02,  4.538225000000e+01,
                    0.000000000000e+00, 7.188560000000e+02, 1.852157000000e+02, -1.130887000000e-01,
                    0.000000000000e+00, 0.000000000000e+00, 1.000000000000e+00,  3.779761000000e-03;

  Eigen::MatrixXd transform_matrix_ = project_matrix*R_rect*RT_velo_to_cam;

  Eigen::Vector4d point;
  point << input(0), input(1), input(2), 1;
  Eigen::Vector3d pimage = transform_matrix_* point;
  cv::Point p2d = cv::Point(pimage(0)/pimage(2),pimage(1)/pimage(2));
  return p2d;
}

Eigen::Vector3d MultiObjectTracking::camera2cloud(const Eigen::Vector3d& input)
{
  Eigen::Matrix4d RT_velo_to_cam;
  Eigen::Matrix4d R_rect;

  RT_velo_to_cam <<  7.967514000000e-03, -9.999679000000e-01, -8.462264000000e-04, -1.377769000000e-02,
                    -2.771053000000e-03,  8.241710000000e-04, -9.999958000000e-01, -5.542117000000e-02,
                     9.999644000000e-01,  7.969825000000e-03, -2.764397000000e-03, -2.918589000000e-01,
                     0.0,                 0.0,                 0.0,                 1.0;
  R_rect <<  9.999454000000e-01, 7.259129000000e-03, -7.519551000000e-03, 0.0,
            -7.292213000000e-03, 9.999638000000e-01, -4.381729000000e-03, 0.0,
             7.487471000000e-03, 4.436324000000e-03,  9.999621000000e-01, 0.0,
             0.0,                0.0,                 0.0,                1.0;

  Eigen::Vector4d point;
  point << input(0), input(1), input(2), 1;
  Eigen::Vector4d pcloud = RT_velo_to_cam.inverse()*R_rect.inverse()* point;
  Eigen::Vector3d result;
  result << pcloud(0),pcloud(1),pcloud(2);
  return result;
}

void MultiObjectTracking::draw3dbox(Detect &det, cv::Mat& image, std::vector<int>& color)
{
	float h = det.box[0];
	float w = det.box[1];
	float l = det.box[2];
        float x = det.position[0];
	float y = det.position[1];
	float z = det.z;
	float yaw = -det.yaw - 90 * M_PI/180;
	double boxroation[9] = {cos(yaw), -sin(yaw), 0, sin(yaw), cos(yaw), 0, 0, 0, 1 };

	Eigen::MatrixXd BoxRotation = Eigen::Map<Eigen::Matrix<double, 3, 3, Eigen::RowMajor> >(boxroation);
	double xAxisP[8] = {l/2,l/2,-l/2,-l/2,l/2,l/2,-l/2,-l/2 };
	double yAxisP[8] = {w/2, -w/2, -w/2, w/2, w/2, -w/2, -w/2, w/2};
	double zAxisP[8] = {h, h, h, h, 0, 0, 0, 0};
	std::vector<cv::Point> imagepoint;
	Eigen::Vector3d translation(x,y,z);

	for (int i = 0; i < 8; i++) {
		Eigen::Vector3d point_3d(xAxisP[i], yAxisP[i], zAxisP[i]);
		Eigen::Vector3d rotationPoint_3d = BoxRotation * point_3d + translation;
		cv::Point imgpoint = cloud2camera(rotationPoint_3d);
		imagepoint.push_back(imgpoint);
	}

	int r = color[0];
	int g = color[1];
	int b = color[2];

	cv::line(image, imagepoint[0], imagepoint[1], cv::Scalar(226, 43, 138), 2, cv::LINE_AA);
	cv::line(image, imagepoint[1], imagepoint[2], cv::Scalar(r, g, b), 2, cv::LINE_AA);
	cv::line(image, imagepoint[2], imagepoint[3], cv::Scalar(r, g, b), 2, cv::LINE_AA);
	cv::line(image, imagepoint[3], imagepoint[0], cv::Scalar(r, g, b), 2, cv::LINE_AA);

	cv::line(image, imagepoint[4], imagepoint[5], cv::Scalar(226, 43, 138), 2, cv::LINE_AA);
	cv::line(image, imagepoint[5], imagepoint[6], cv::Scalar(r, g, b), 2, cv::LINE_AA);
	cv::line(image, imagepoint[6], imagepoint[7], cv::Scalar(r, g, b), 2, cv::LINE_AA);
	cv::line(image, imagepoint[7], imagepoint[4], cv::Scalar(r, g, b), 2, cv::LINE_AA);

	cv::line(image, imagepoint[0], imagepoint[4], cv::Scalar(226, 43, 138), 2, cv::LINE_AA);
	cv::line(image, imagepoint[1], imagepoint[5], cv::Scalar(226, 43, 138), 2, cv::LINE_AA);
	cv::line(image, imagepoint[2], imagepoint[6], cv::Scalar(r, g, b), 2, cv::LINE_AA);
	cv::line(image, imagepoint[3], imagepoint[7], cv::Scalar(r, g, b), 2, cv::LINE_AA);

	cv::line(image, imagepoint[0], imagepoint[5], cv::Scalar(226, 43, 138), 2, cv::LINE_AA);
	cv::line(image, imagepoint[1], imagepoint[4], cv::Scalar(226, 43, 138), 2, cv::LINE_AA);

	imagepoint.clear();
}

int64_t MultiObjectTracking::gtm() {
	struct timeval tm;
	gettimeofday(&tm, 0);
	// return ms
	int64_t re = (((int64_t) tm.tv_sec) * 1000 * 1000 + tm.tv_usec);
	return re;
}
