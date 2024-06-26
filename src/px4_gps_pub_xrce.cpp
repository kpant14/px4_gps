#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <eigen3/Eigen/Eigen>
#include <gz/math.hh>
#include <gz/msgs.hh>
#include <gz/transport.hh>
#include <gz/msgs/navsat_multipath.pb.h>
#include <gz/msgs/pose_v.pb.h>
#include "rclcpp/rclcpp.hpp"
#include <rclcpp/qos.hpp>
#include "std_msgs/msg/string.hpp"
#include "px4_msgs/msg/sensor_gps.hpp"
#include <gz/sensors/NavSatMultipathSensor.hh>
#include <geometry_msgs/msg/pose.hpp>
#include <std_msgs/msg/bool.hpp>
#include "offboard_detector/msg/detector_output.hpp"
using namespace std::chrono_literals;

/* This example creates a subclass of Node and uses std::bind() to register a
* member function as a callback from the timer. */

class PX4GPSXrcePublisher : public rclcpp::Node
{
  public:
    PX4GPSXrcePublisher()
    : Node("px4_gps_xrce_publisher")
    {
      this->declare_parameter("px4_ns", "px4_1");
      this->declare_parameter("gz_world_name", "AbuDhabi");
      this->declare_parameter("gz_model_name", "x500_1");
      this->declare_parameter("gz_spoofer_model_name", "spoofer");

      std::string _px4_ns = this->get_parameter("px4_ns").as_string();
      std::string _world_name = this->get_parameter("gz_world_name").as_string();
      std::string _model_name = this->get_parameter("gz_model_name").as_string();
      std::string _spoofer_model_name = this->get_parameter("gz_spoofer_model_name").as_string();

      RCLCPP_INFO(this->get_logger(),"PX4 Namespace: %s", _px4_ns.c_str());
      RCLCPP_INFO(this->get_logger(),"World Name: %s", _world_name.c_str());
      RCLCPP_INFO(this->get_logger(),"Drone Model Name: %s", _model_name.c_str());
      RCLCPP_INFO(this->get_logger(),"Spoofer Model Name: %s", _spoofer_model_name.c_str());
      
      std::string navsat_topic = "/world/" + _world_name + "/model/" + _model_name + "/link/base_link/sensor/navsat_sensor/navsat_multipath";
      std::string spoofer_topic = "/world/" + _world_name + "/model/" + _spoofer_model_name + "/link/base_link/sensor/navsat_sensor/navsat_multipath";
      
      std::string px4_gps_ros_topic= "";
      if (_px4_ns.empty()){
        px4_gps_ros_topic = "/fmu/in/vehicle_gps_position";
      }
      else{
        px4_gps_ros_topic = "/" +  _px4_ns + "/fmu/in/vehicle_gps_position";
      }
      RCLCPP_INFO(this->get_logger(),"PX4 GPS Topic: %s", px4_gps_ros_topic.c_str());

      if (!_node.Subscribe(navsat_topic, &PX4GPSXrcePublisher::navsatCallback, this)) {
            RCLCPP_ERROR(this->get_logger(),"failed to subscribe to %s", navsat_topic.c_str());
      }
      if (!_node.Subscribe(spoofer_topic, &PX4GPSXrcePublisher::navsatSpooferCallback, this)) {
            RCLCPP_ERROR(this->get_logger(),"failed to subscribe to %s", spoofer_topic.c_str());
      }  
      rmw_qos_profile_t qos_profile = rmw_qos_profile_default;
      qos_profile.history=RMW_QOS_POLICY_HISTORY_KEEP_LAST;
      qos_profile.depth=1;
      qos_profile.reliability=RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
      qos_profile.durability=RMW_QOS_POLICY_DURABILITY_VOLATILE;
      auto qos_ = rclcpp::QoS(rclcpp::QoSInitialization(qos_profile.history, qos_profile.depth), qos_profile);
      
      gps_publisher_ = this->create_publisher<px4_msgs::msg::SensorGps>(px4_gps_ros_topic, 10);  
      attack_flag_subscriber_ = this->create_subscription<std_msgs::msg::Bool>(
        "/attack_flag", 10, std::bind(&PX4GPSXrcePublisher::attackFlagCallback, this, std::placeholders::_1));      
      detector_flag_subscriber_ = this->create_subscription<offboard_detector::msg::DetectorOutput>(
        "/px4_1/detector/out/observer_output", qos_, std::bind(&PX4GPSXrcePublisher::detectorFlagCallback, this, std::placeholders::_1));
      count=0;
    }
  private:
    void navsatCallback(const gz::msgs::NavSatMultipath &navsat)
    {   
        const uint64_t time_us = (navsat.header().stamp().sec() * 1000000) + (navsat.header().stamp().nsec() / 1000);
        // publish gps
        auto sensor_gps = px4_msgs::msg::SensorGps();
        sensor_gps.device_id =11468804; // (Type: 0xAF, SIMULATION:0 (0x00))
        sensor_gps.timestamp_sample = time_us;
        sensor_gps.timestamp = time_us;
        sensor_gps.fix_type = 3; // 3D fix
        sensor_gps.s_variance_m_s = 0.4f;
        sensor_gps.c_variance_rad = 0.1f;
        sensor_gps.eph = 0.9f;
        sensor_gps.epv = 1.78f;
        sensor_gps.hdop = 0.7f;
        sensor_gps.vdop = 1.1f;
        sensor_gps.altitude_msl_m = navsat.altitude();
        sensor_gps.altitude_ellipsoid_m = navsat.altitude();
        sensor_gps.noise_per_ms = 0;
        sensor_gps.jamming_indicator = 0;
        // For normal operations
        sensor_gps.latitude_deg = navsat.latitude_deg();
        sensor_gps.longitude_deg = navsat.longitude_deg();
        sensor_gps.vel_m_s = sqrtf(
                navsat.velocity_east() * navsat.velocity_east()
                + navsat.velocity_north() * navsat.velocity_north()
                + navsat.velocity_up() * navsat.velocity_up());
        sensor_gps.vel_n_m_s = navsat.velocity_north();
        sensor_gps.vel_e_m_s = navsat.velocity_east();
        sensor_gps.vel_d_m_s = -navsat.velocity_up();
        sensor_gps.cog_rad = atan2(navsat.velocity_east(), navsat.velocity_north());
        if (attack_flag == 1 )//&& attack_flag == 0)
        {
          sensor_gps.latitude_deg = spoofer_latitude;
          sensor_gps.longitude_deg = spoofer_longitude;
        }
        sensor_gps.timestamp_time_relative = 0;
        sensor_gps.heading = NAN;
        sensor_gps.heading_offset = NAN;
        sensor_gps.heading_accuracy = 0;
        sensor_gps.automatic_gain_control = 0;
        sensor_gps.jamming_state = 0;
        sensor_gps.spoofing_state = 0;
        sensor_gps.vel_ned_valid = true;
        sensor_gps.satellites_used = 10;
        count++;
        gps_publisher_->publish(sensor_gps);
    }
    
    void attackFlagCallback(const std_msgs::msg::Bool::SharedPtr msg)
    {
      attack_flag = msg->data;
    }
    
    void detectorFlagCallback(const offboard_detector::msg::DetectorOutput::SharedPtr msg)
    {
      detector_flag = msg->attack_detect;
      attack_flag = 0;
    }
    
    void navsatSpooferCallback(const gz::msgs::NavSatMultipath &msg_spoofer)
    { 
      spoofer_latitude = msg_spoofer.latitude_deg();
      spoofer_longitude =  msg_spoofer.longitude_deg();
      spoofer_velocity_east = msg_spoofer.velocity_east();
      spoofer_velocity_north = msg_spoofer.velocity_north();            
    }
  
    rclcpp::Publisher<px4_msgs::msg::SensorGps>::SharedPtr gps_publisher_;
    rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr attack_flag_subscriber_;
    rclcpp::Subscription<offboard_detector::msg::DetectorOutput>::SharedPtr detector_flag_subscriber_;
    gz::transport::Node _node;
    int count;
    double spoofer_latitude;
    double spoofer_longitude;
    double spoofer_velocity_east;
    double spoofer_velocity_north;
    int attack_flag;
    int detector_flag;
    
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PX4GPSXrcePublisher>());
  rclcpp::shutdown();
  return 0;
}
