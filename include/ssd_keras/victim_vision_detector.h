#ifndef SSD_DETECTION_H
#define SSD_DETECTION_H

#include <iostream>
#include <ros/ros.h>
#include <ros/package.h>
#include <iostream>
#include <sstream>
#include "std_msgs/Int32.h"



#include <string>

#include <Eigen/Geometry>
#include <sensor_msgs/PointCloud2.h>

#include <sensor_msgs/Image.h>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include "opencv2/objdetect/objdetect.hpp"
#include <geometry_msgs/PoseStamped.h>

#include <pcl/ModelCoefficients.h>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/features/normal_3d.h>
#include <pcl/kdtree/kdtree.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/filters/filter.h>
#include <pcl/segmentation/extract_clusters.h>
#include <pcl/registration/icp.h>
#include <pcl/filters/passthrough.h>
#include <pcl/common/centroid.h>

//sync_time
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/time_synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

//tf
#include <tf/transform_listener.h>
#include <tf/transform_datatypes.h>
#include <tf_conversions/tf_eigen.h>
#include <tf/transform_listener.h>
#include <pcl_ros/transforms.h>
#include <pcl/point_types.h>

#include <victim_localization/common.h>
#include "utilities/pose_conversion.h"

//custom msgs
#include <victim_localization/DL_msgs_box.h>
#include <victim_localization/DL_msgs_boxes.h>
#include "victim_localization/DL_box.h"

#include <victim_localization/victim_detector_base.h>


class victim_vision_detector : public victim_detector_base
{
public:
  victim_vision_detector();
  ~victim_vision_detector();
  cv_bridge::CvImagePtr current_depth_image;
  victim_localization::DL_msgs_box current_ssd_detection;
  geometry_msgs::Point detected_point;
  bool detection_Cluster_succeed; //
  ros::Publisher pub_segemented_human_pointcloud;
  ros::Publisher pub_setpoint;
  double image_x_resol;
  double image_y_resol;
  double image_x_offset;
  double image_y_offset;
  double RGB_FL;
  std::string camera_optical_frame;
  bool ServiceCallFirstTime;
  ros::Time Start_time;
  ros::Duration Service_startTime;


  //added for ssd client
  bool Detection_success;
  victim_localization::DL_box srv;
  ros::ServiceClient client ;
  void DetectionService();

  ros::NodeHandle nh_;
  ros::NodeHandle nh_private;
  tf::TransformListener *tf_listener;
  typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::Image,geometry_msgs::PoseStamped> MySyncPolicy;
  message_filters::Subscriber<sensor_msgs::Image> *depth_in_;
  message_filters::Subscriber<geometry_msgs::PoseStamped> *loc_sub_;
  message_filters::Synchronizer<MySyncPolicy> *sync;

  bool DetectionAvailable();
  void CallBackData(const sensor_msgs::ImageConstPtr& input_depth,
                    const geometry_msgs::PoseStamped::ConstPtr& loc);
  void FindClusterCentroid();
  Status getDetectorStatus();
  geometry_msgs::Point getClusterCentroid();
  void PublishSegmentedPointCloud(const pcl::PointCloud<pcl::PointXYZ> input_PointCloud);
  void performDetection();
  ros::Duration GetConnectionTime();
};

#endif // SSD_DETECTION_H
