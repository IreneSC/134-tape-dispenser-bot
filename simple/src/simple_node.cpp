#include "ros/ros.h"
#include "std_msgs/Float64.h"
#include "std_msgs/Bool.h"
#include "sensor_msgs/JointState.h"
#include "hebiros/EntryListSrv.h"
#include "hebiros/AddGroupFromNamesSrv.h"
#include "hebiros/SizeSrv.h"
#include "simple/FaceTrack.h"
#include <cmath>

using namespace hebiros;


/*
**   Global Variables.  So the callbacks can pass information.
*/
sensor_msgs::JointState feedback;       // The actuator feedback struccture
volatile int            feedbackvalid = 0;
volatile double         goalpos;        // The goal position
volatile int 			  valid;
volatile int 		    isValidPrev = 1;


/*
**   Feedback Subscriber Callback
*/
void feedbackCallback(sensor_msgs::JointState data)
{
  feedback = data;
  ROS_INFO("FBK pos [%f]", feedback.position[0]);
  feedbackvalid = 1;
}


/*
**   Goal Subscriber Callback
*/
void goalCallback(const std_msgs::Float64::ConstPtr& msg)
{
  goalpos = msg->data;
  ROS_INFO("I heard: [%f]", (double) msg->data);
}

/*
**   Valid goal Subscriber Callback
*/
void validCallback(const std_msgs::Bool::ConstPtr& msg)
{
  isValidPrev = valid;
  valid = msg->data;
  ROS_INFO("I heard: [%d]", (int) msg->data);
}

/*
**   Main Code
*/
int main(int argc, char **argv)
{
  // Initialize the basic ROS node, run at 200Hz.
  ros::init(argc, argv, "simple_node");
  ros::NodeHandle n;
  ros::Rate loop_rate(200);


  // Ask the Hebi node to list the modules.  Create a client to their
  // service, instantiate a service class, and call.  This has no
  // input or output arguments.
  ros::ServiceClient entry_list_client = n.serviceClient<EntryListSrv>("/hebiros/entry_list");
  EntryListSrv entry_list_srv;
  entry_list_client.call(entry_list_srv);


  // Create a new "group of actuators".  This has input arguments,
  // which are the names of the actuators.
  std::string group_name = "all";
  ros::ServiceClient add_group_client = n.serviceClient<AddGroupFromNamesSrv>("/hebiros/add_group_from_names");
  AddGroupFromNamesSrv add_group_srv;
  add_group_srv.request.group_name = group_name;
  add_group_srv.request.names = {"tapedispenser"};
  add_group_srv.request.families = {"Arm"};
  ROS_INFO("about to client");
  // Repeatedly call the service until it succeeds.
  while(!add_group_client.call(add_group_srv)) ;

  // Check the size of this group.  This has an output argument.
  ros::ServiceClient size_client = n.serviceClient<SizeSrv>("/hebiros/"+group_name+"/size");
  SizeSrv size_srv;
  size_client.call(size_srv);
  ROS_INFO("%s has been created and has size %d", group_name.c_str(), size_srv.response.size);



  // Create a subscriber to listen for a goal.
  ros::Subscriber goalSubscriber = n.subscribe("/goal", 100, goalCallback);
  ros::Subscriber validSubscriber = n.subscribe("/valid", 100, validCallback);

  // Create a subscriber to receive feedback from the actuator group.
  ros::Subscriber feedback_subscriber = n.subscribe("/hebiros/"+group_name+"/feedback/joint_state", 100, feedbackCallback);

  feedback.position.reserve(1);
  feedback.velocity.reserve(1);
  feedback.effort.reserve(1);


  // Create a publisher to send commands to the actuator group.
  ros::Publisher command_publisher = n.advertise<sensor_msgs::JointState>("/hebiros/"+group_name+"/command/joint_state", 100);



  sensor_msgs::JointState command_msg;
  command_msg.name.push_back("Arm/tapedispenser");
  command_msg.position.resize(1);
  command_msg.velocity.resize(1);
  command_msg.effort.resize(1);


  // Wait until we have some feedback from the actuator.
  ROS_INFO("Waiting for initial feedback");
  while (!feedbackvalid)
    {
      ros::spinOnce();
      loop_rate.sleep();
    }


  // Prep the servo loop.
  double  dt = loop_rate.expectedCycleTime().toSec();
  double time = 0;
  double freqHz = .12;
  double freq = M_PI * 2 * freqHz;
  double amp = M_PI/2;

  double  speed = 0.5;          // Speed to reach goal.
  double  cmdpos = feedback.position[0];
  double  cmdvel = 0.0;

  // Start where we are.
  goalpos = cmdpos;
  double fixed_goal_pos = goalpos;
  double initpos = cmdpos;

  // Run the servo loop until shutdown.
  ROS_INFO("Running the servo loop with dt %f", dt);
  while(ros::ok())
    {

      // Decides to track face or to follow sinusoid
	  if (!valid) {
	  // sinusoid
	  	if (isValidPrev) {
	  	  // Restart Sinusoid
	  	  initpos = feedback.position[0];
	  	  time = 0;
	  	}
	    fixed_goal_pos = initpos - amp + amp * cos(time*freq);

	  }
    else {
        fixed_goal_pos = goalpos + feedback.position[0];
    }

      // Move the goal.
      if      (cmdpos < fixed_goal_pos - speed*dt)   cmdvel = speed;
      else if (cmdpos > fixed_goal_pos + speed*dt)   cmdvel = -speed;
      else                                    cmdvel = (fixed_goal_pos - cmdpos)/dt;
      cmdpos += dt * cmdvel;

      // info
      //ROS_INFO("Command position [%f]", cmdpos);

      // Apply.
      ROS_INFO("Target position: %f", cmdpos);
      command_msg.position[0] = cmdpos;
      command_msg.velocity[0] = cmdvel;
      command_msg.effort[0]   = 0;
      command_publisher.publish(command_msg);

      // Wait for next turn.
      ros::spinOnce();
      loop_rate.sleep();
      time += dt;
    }

  return 0;
}
