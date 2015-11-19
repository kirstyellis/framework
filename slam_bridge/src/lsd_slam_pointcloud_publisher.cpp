/*
* Software License Agreement (BSD License)
*
* Copyright (c) 2014,DLO, Wageningen University & Research Center.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*
* * Redistributions of source code must retain the above copyright
* notice, this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above
* copyright notice, this list of conditions and the following
* disclaimer in the documentation and/or other materials provided
* with the distribution.
* * Neither the name of Open Source Robotics Foundation nor
* the names of its contributors may be used to endorse or promote
* products derived from this software without specific prior
* written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
* ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/



                        /*,-:;//;:=,
                    . :H@@@MM@M#H/.,+%;,
                 ,/X+ +M@@M@MM%=,-%HMMM@X/,
                -+@MM; $M@@MH+-,;XMMMM@MMMM@+-
               ;@M@@M- XM@X;. -+XXXXXHHH@M@M#@/.
             ,%MM@@MH ,@%=            .---=-=:=,.
             =@#@@@MX .,              -%HX$$%%%+;
            =-./@M@M$                  .;@MMMM@MM:
           X@/ -$MM/                    .+MM@@@M$
          ,@M@H: :@:                    . =X#@@@@-
          ,@@@MMX, .                    /H- ;@M@M=
          .H@@@@M@+,                    %MM+..%#$.
           /MMMM@MMH/.                  XM@MH; =;
            /%+%$XHH@$=              , .H@@@@MX,
            .=--------.           -%H.,@@@@@MX,
              %MM@@@HHHXX$$$%+- .:$MMX =M@@MM%.
               =XMMM@MM@MM#H;,-+HMM@M+ /MMMX=
                  =%@M@M#@$-.=$@MM@@@M; %M%=
                    ,:+$+-,/H#MMMMMMM@= =,
                         =++%%%%+/:-*/


#include "lsd_slam_pointcloud_publisher.h"


/*
*   Callback when a pointcloud from LSD-SLAM is received.
*   Function transforms it to the sensor_msgs/PointCloud format.
*   TODO: use sensor_msgs/PointCloud2 or templated version instead.
*/
void SlamBridge::pointcloudCallback(const slam_bridge::keyframeMsg::ConstPtr &raw_point_cloud_message)
{
  ROS_INFO("Recieved a pointcloud.");

  // Track if points are already transformed.
  bool transformed = false;

  originalInput = new InputPointDense[raw_point_cloud_message->width * raw_point_cloud_message->height];
  memcpy(originalInput, raw_point_cloud_message->pointcloud.data(),
         raw_point_cloud_message->width * raw_point_cloud_message->height * sizeof(InputPointDense));

  // If there are no points then we're done.
  if (originalInput == 0)
    return;

  // The raw pointcloud processed into this processed pointcloud.
  sensor_msgs::PointCloud processed_pointcloud;
  processed_pointcloud.header.stamp.sec = raw_point_cloud_message->time;

  // Get pose from received lsd-slam pointcloud relative to starting pose of lsd-slam.
  memcpy(camToWorld.data(), raw_point_cloud_message->camToWorld.data(), 7 * sizeof(float));

  // Get camera parameters from lsd-slam node. Needed to compute 3D coordinates.
  double fx = raw_point_cloud_message->fx;
  double fy = raw_point_cloud_message->fy;
  double cx = raw_point_cloud_message->cx;
  double cy = raw_point_cloud_message->cy;

  double fxi = 1 / fx;
  double fyi = 1 / fy;
  double cxi = -cx / fx;
  double cyi = -cy / fy;

  sensor_msgs::ChannelFloat32 channel_red;
  sensor_msgs::ChannelFloat32 channel_green;
  sensor_msgs::ChannelFloat32 channel_blue;

  channel_red.name = "r";
  channel_green.name = "g";
  channel_blue.name = "b";

  // For each point in the recieved lsd-slam pointcloud message,
  // calculate its 3D real world coordinate. Formerly they were image coordinates.
  for (int y = 1; y < raw_point_cloud_message->height - 1; y++)
  {
    for (int x = 1; x < raw_point_cloud_message->width - 1; x++)
    {
      // Skip all points with zero depths.
      if (originalInput[x + y * raw_point_cloud_message->width].idepth <= 0)
      {
        continue;
      }

      // Skip adding this point with a random chance.
      if (sparsity_factor > 1 && rand() % sparsity_factor != 0)
      {
        continue;
      }

      // Calculate the depth of the point.
      float depth = 1 / originalInput[x + y * raw_point_cloud_message->width].idepth;

      // Depending on if you want to transform the pointcloud using
      // the lsd-slam pose or a pose of baxter's end-effector back in time.
      geometry_msgs::Point32 point;
      if (lsd_slam_frame_transformed)
      {
        // Create 3D point, with lsd-slam pose transformation.
        Sophus::Vector3f transformed_point =
            camToWorld * (Sophus::Vector3f((x * fxi + cxi), (y * fyi + cyi), 1) * depth);
        point.x = transformed_point[0];
        point.y = transformed_point[1];
        point.z = transformed_point[2];

        // Add color to point.
        //channel_red.values.push_back(originalInput[x+y*raw_point_cloud_message->width].color[0]   / 255.0);
        //channel_green.values.push_back(originalInput[x+y*raw_point_cloud_message->width].color[1] / 255.0);
        //channel_blue.values.push_back(originalInput[x+y*raw_point_cloud_message->width].color[2]  / 255.0);

      }
      else
      {
        // Create 3D point, without lsd-slam pose transformation.
        // Use baxter's endpoint pose in the past when image was taken instead. See below.
        geometry_msgs::Point32 point;
        point.x = (x * fxi + cxi) * depth;
        point.y = (y * fyi + cyi) * depth;
        point.z = depth;
      }

      // Add point to pointcloud.
      processed_pointcloud.points.push_back(point);
    }
  }

  // Add color channels to pointcloud.
  //processed_pointcloud.channels.push_back(channel_red);
  //processed_pointcloud.channels.push_back(channel_green);
  //processed_pointcloud.channels.push_back(channel_blue);

  // We need to transform the processed pointcloud to the base frame.
  // For this we use the known end-point pose when the source image was taken used for
  // the lsd-slam node to calculate this received pointcloud.
  // If the pointcloud was transformed using the lsd-slam pose, then output it directly.
  if (!lsd_slam_frame_transformed)
  {
    // First transform the received pointcloud from the frame it was taken in (robot gripper frame)
    // to the world frame (base). Note that the time is important here when looking up the
    // transform. The time should be when the image was taken of the keyframe the received pointcloud
    // belongs to.
    processed_pointcloud.header.frame_id = "camera";
    sensor_msgs::PointCloud transformed_pointcloud;
    try
    {
      ROS_INFO("%s", processed_pointcloud.header.frame_id.c_str());
      ROS_INFO("%f", processed_pointcloud.header.stamp.toSec());

      // Wait (max 3 sec) until transform comes available in the system. There are always slight delays.
      transform_listener.waitForTransform("delta_base", processed_pointcloud.header.frame_id,
                                          processed_pointcloud.header.stamp, ros::Duration(3.0));
      transform_listener.transformPointCloud("delta_base", ros::Time(0), processed_pointcloud,
                                             processed_pointcloud.header.frame_id, transformed_pointcloud);

    }
    catch (tf::TransformException ex) {
      ROS_WARN("Could not transform recieved slam pointcloud from '%s' to 'delta_base' frame : %s",
               processed_pointcloud.header.frame_id.c_str(), ex.what());
    }

    // Transform pointcloud to pointcloud2 message.
    sensor_msgs::PointCloud2 transformed_pointcloud2;
    sensor_msgs::convertPointCloudToPointCloud2(transformed_pointcloud, transformed_pointcloud2);
    transformed_pointcloud2.header.frame_id = "camera";
    output_pointcloud_publisher.publish(transformed_pointcloud2);
  }
  else
  {
    // First pointcloud is set as starting reconstructed scene pointcloud.
    // All next pointclouds are merged with that one.
    if (reconstructed_scene_pointcloud2.data.size() == 0)
    {
      // Transform pointcloud to pointcloud2 message.
      sensor_msgs::PointCloud2 processed_pointcloud2;
      sensor_msgs::convertPointCloudToPointCloud2(processed_pointcloud, processed_pointcloud2);
      reconstructed_scene_pointcloud2 = processed_pointcloud2;
    }
    else
    {
      // Transform ROS pointcloud to ROS pointcloud2 message.
      sensor_msgs::PointCloud2 processed_pointcloud2;
      sensor_msgs::convertPointCloudToPointCloud2(processed_pointcloud, processed_pointcloud2);



      // Merge the ROS pointcloud2 with previous pointclouds to reconstruct scene.
      //sensor_msgs::PointCloud2 concatenated_pointcloud2;
      //pcl::concatenatePointCloud(reconstructed_scene_pointcloud2, processed_pointcloud2, concatenated_pointcloud2);
      //reconstructed_scene_pointcloud2 = concatenated_pointcloud2;

      /*
      // Filter for downsampling.
      sensor_msgs::PointCloud2::Ptr input (new sensor_msgs::PointCloud2(reconstructed_scene_pointcloud2));
      sensor_msgs::PointCloud2::Ptr downsampled_pointcloud_pcl (new sensor_msgs::PointCloud2);
      pcl::VoxelGrid<sensor_msgs::PointCloud2> downsample_filter;
      downsample_filter.setInputCloud (input);
      downsample_filter.setLeafSize (0.0075f, 0.0075f, 0.075f);
      downsample_filter.filter(*downsampled_pointcloud_pcl);
      reconstructed_scene_pointcloud2 = *downsampled_pointcloud_pcl;
      */

      /*
      // Filter outlier if point has only a few neighbors.
      sensor_msgs::PointCloud2::Ptr input (new sensor_msgs::PointCloud2(processed_pointcloud2));
      sensor_msgs::PointCloud2::Ptr filtered_radiusoutlier_pointcloud_pcl (new sensor_msgs::PointCloud2);
      pcl::RadiusOutlierRemoval<sensor_msgs::PointCloud2> outlier_neighbor_filter;
      outlier_neighbor_filter.setInputCloud(input);
      outlier_neighbor_filter.setRadiusSearch(0.10);
      outlier_neighbor_filter.setMinNeighborsInRadius(3);
      outlier_neighbor_filter.filter(*filtered_radiusoutlier_pointcloud_pcl);
      reconstructed_scene_pointcloud2 = *filtered_radiusoutlier_pointcloud_pcl;
      */

      /*
      // Filter outlier if point is to far from the mean of neighbors.
      sensor_msgs::PointCloud2::Ptr input_2 (new sensor_msgs::PointCloud2(reconstructed_scene_pointcloud2));
      sensor_msgs::PointCloud2::Ptr filtered_statisticaloutlier_pointcloud_pcl (new sensor_msgs::PointCloud2());
      pcl::StatisticalOutlierRemoval<sensor_msgs::PointCloud2> outlier_statistical_filter(false);
      outlier_statistical_filter.setInputCloud (input_2);
      outlier_statistical_filter.setMeanK (10);
      outlier_statistical_filter.setStddevMulThresh (1.0);
      outlier_statistical_filter.setNegative(false);
      outlier_statistical_filter.filter (*filtered_statisticaloutlier_pointcloud_pcl);
      reconstructed_scene_pointcloud2 = *filtered_statisticaloutlier_pointcloud_pcl;
      */

      // Publish latest reconstructed scene in appropiate frame.
      processed_pointcloud2.header.frame_id = "camera";
      processed_pointcloud2.header.stamp.sec = raw_point_cloud_message->time;
      output_pointcloud_publisher.publish(processed_pointcloud2);
    }
  }
}


/*
*   Callback that listens to the current pose from the SLAM node.
*   Note that this pose is relative to a starting frame and hence
*   cannot be accurately used without proper calibration.
*/
void SlamBridge::poseCallback(const geometry_msgs::PoseStamped::ConstPtr &slam_pose)
{
  // Not used yet.
}


/*
*   Service client function to request to endpoint state of the robot.
*   This information is needed to transform the pointcloud that was
*   received by the SLAM node. 
*/
bool SlamBridge::requestEndPointState()
{
  std::string request_message = "trigger";
  autopic_msgs::RequestCameraState service;
  service.request.request_message = request_message;
  if (robot_endpointstate_client.call(service))
  {
    robot_endpointstate_pose = service.response.pose;
    return true;
  }
  else
  {
    return false;
  }
}


SlamBridge::SlamBridge() {
  // Factor of point in a new pointcloud that is merged with the reconstructed scene.
  sparsity_factor = 1;
  node_handle.getParam("sparsity_factor", sparsity_factor);

  // Bool that either tells the bridge to use a pose transformation
  // based lsd-slam pose (true) or baxter's published pose (false).
  lsd_slam_frame_transformed = true;

  // Frame that the reconstructed pointcloud is published in.
  node_handle.getParam("frame", frame);

  robot_endpointstate_client = node_handle.serviceClient<autopic_msgs::RequestCameraState>("camerastate");

  raw_pointcloud_subcriber = node_handle.subscribe("/lsd_slam/keyframes", 1000, &SlamBridge::pointcloudCallback, this);
  slam_pose_subcriber = node_handle.subscribe("/lsd_slam/pose", 1000, &SlamBridge::poseCallback, this);

  output_pointcloud_publisher = node_handle.advertise<sensor_msgs::PointCloud2>("/slam_pointcloud", 1000);

  // Dynamic reconfigure lsd-slam node.
  // TODO: fix launchfile parameter reading.
  dynamic_reconfigure::ReconfigureRequest srv_req;
  dynamic_reconfigure::ReconfigureResponse srv_resp;
  dynamic_reconfigure::DoubleParameter parameter;
  dynamic_reconfigure::Config configuration;

  double minUseGrad;
  node_handle.getParam("/lsd_slam_pointcloud_publisher/minUseGrad", minUseGrad);
  parameter.name = "minUseGrad";
  parameter.value = minUseGrad;
  configuration.doubles.push_back(parameter);

  double cameraPixelNoise;
  node_handle.getParam("/lsd_slam_pointcloud_publisher/cameraPixelNoise", cameraPixelNoise);
  parameter.name = "cameraPixelNoise";
  parameter.value = cameraPixelNoise;
  configuration.doubles.push_back(parameter);

  double KFUsageWeight;
  node_handle.getParam("/lsd_slam_pointcloud_publisher/KFUsageWeight", KFUsageWeight);
  parameter.name = "KFUsageWeight";
  parameter.value = KFUsageWeight;
  configuration.doubles.push_back(parameter);

  double KFDistWeight;
  node_handle.getParam("/lsd_slam_pointcloud_publisher/KFDistWeight", KFDistWeight);
  parameter.name = "KFDistWeight";
  parameter.value = KFDistWeight;
  configuration.doubles.push_back(parameter);

  // TODO: Add loop until confirmed. Use regular service structure.
  srv_req.config = configuration;
  ros::service::call("/LSD_SLAM/set_parameters", srv_req, srv_resp);
}


/*
*   This ROS node creates a bridge between the lsd-slam pointcloud and ROS pointcloud.
*   When a lsd-slam pointcloud is published, the bridge automatically processes and
*   publishes it as a ROS pointcloud.
*/
int main(int argc, char **argv) {
  // Initialize this ROS node.
  ros::init(argc, argv, "lsd_slam_pointcloud_publisher");

  // Create an instantiation of the bridge that transforms an 
  // incoming LSD-SLAM pointcloud to a ROS pointcloud.
  SlamBridge slam_bridge;

  // Loop to ensure the pose of the robot's endpoint are known.
  bool endpoints_set = false;
  while (!endpoints_set)
  {
    // Request the current pose of the robot endpoint state.
    // Used to properly transform the LSD-SLAM pointcloud to global frame.
    endpoints_set = slam_bridge.requestEndPointState();
  }

  // Set transformation once here as the initial pose of the robot's end-effector.
  // The pointclouds of lsd-slam will be produced in one frame that is relative to
  // the one that is taken in the first image. 
  tf::TransformBroadcaster tf_broadcaster;
  tf::Transform transform;
  tf::Vector3 starting_position = tf::Vector3(slam_bridge.robot_endpointstate_pose.pose.position.x,
                                              slam_bridge.robot_endpointstate_pose.pose.position.y,
                                              slam_bridge.robot_endpointstate_pose.pose.position.z);
  tf::Quaternion starting_orientation = tf::Quaternion(slam_bridge.robot_endpointstate_pose.pose.orientation.x,
                                                       slam_bridge.robot_endpointstate_pose.pose.orientation.y,
                                                       slam_bridge.robot_endpointstate_pose.pose.orientation.z,
                                                       slam_bridge.robot_endpointstate_pose.pose.orientation.w);
  transform.setOrigin(starting_position);
  transform.setRotation(starting_orientation);

  // The ROS node will now enter the following loop.
  while (slam_bridge.node_handle.ok())
  {
    // Frame transformations need to be updated constantly, otherwise they will fade out of existence.
    tf_broadcaster.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "delta_base", "slam_pointcloud"));

    // Do one spin of the rosnode, e.g. checking and executing callbacks.
    // (http://wiki.ros.org/roscpp/Overview/Callbacks%20and%20Spinning)
    ros::spinOnce();
  }
  return 0;
}
