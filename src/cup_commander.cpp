#include "ros/ros.h"
#include "geometry_msgs/PointStamped.h"
#include "geometry_msgs/PoseStamped.h"
#include "geometry_msgs/PoseArray.h"
#include "sensor_msgs/JointState.h"
#include "std_msgs/Bool.h"
#include "bar_bot/Mobility.h"
#include "bar_bot/Detections.h"
#include "std_msgs/String.h"
#include <unordered_map>
#include <vector>

// Topics
static std::string detection_topic;
static std::string target_position_topic;
static std::string target_gripper_state_topic;
static std::string current_pose_topic;
static std::string drink_type_topic("/drink_type_topic");

// Pub/sub/clients
static ros::ServiceClient mobility_client;
static ros::Subscriber detection_subscriber;
static ros::Subscriber current_pose_subscriber;
static ros::Subscriber drink_subscriber;


// Things gotten from topics
// static geometry_msgs::PoseArray cup_pose_array;
// static geometry_msgs::PoseArray empty_cup_pose_array;
static geometry_msgs::PointStamped last_arm_pos;
// static ros::Time last_cup_detection(0);
// static ros::Duration detection_expiration(1.0);

// Set up map of detections
static const std::string CUP("Cup");
static const std::string SPRITE("Sprite");
static const std::string VODKA("Vodka");
static const std::string OJ("Orange Juice");
static const std::string TEQUILA("Tequila");
static const std::string GRENADINE("Grenadine");
static const std::string SALTYY("Salt");


static std::unordered_map<std::string, geometry_msgs::Point> det_positions;
// mixed drink definitions
static const std::vector<std::string> MARGARITA{SALTYY, TEQUILA, OJ};
static const std::vector<std::string> SCREWDRIVER{VODKA, OJ};
static const std::vector<std::string> TEQUILA_SUNRISE{TEQUILA, GRENADINE, OJ, SPRITE};
static const std::vector<std::string> SUNRISE{GRENADINE, OJ, SPRITE};
static const std::vector<std::string> VODKA_SPRITE{SPRITE, VODKA};
static const std::vector<std::string> TEQUILA_SPRITE{SPRITE, TEQUILA};
static const std::vector<std::string> SHIRLEY_TEMPLE{SPRITE, GRENADINE};
static const std::vector<std::string> ZOO{SPRITE, VODKA, OJ, TEQUILA, GRENADINE};
static std::unordered_map<std::string, std::vector<std::string>> det_ingredients {
    {"margarita", MARGARITA},
    {"screwdriver", SCREWDRIVER},
    {"tequilasunrise", TEQUILA_SUNRISE},
    {"sunrise", SUNRISE},
    {"vodka sprite", VODKA_SPRITE},
    {"tequilasprite", TEQUILA_SPRITE},
    {"shirleytemple", SHIRLEY_TEMPLE},
    {"zoo", ZOO}
};

// Function definitions
static bool moveWithFailureChecking(bar_bot::Mobility mobility);

// Other constants
static geometry_msgs::Point end_point;

static void backAndForth();
static void trackCups();
static void pourBeer();
static void scanForObject(std::string target, bool close_gripper);

static void goHomeCup(bool close_gripper = false);
static void goHomeBottle(bool close_gripper = false);

static void pourDrinkIntoCup(std::string drink);

static geometry_msgs::Point retrieveDrink(std::string drink);
static void pourIntoTarget(std::string drink);
static void replaceDrink(std::string drink);

static void saltTheCup();

static void pourMixedDrink();
static void processDrinkRequest(const std_msgs::String& drinkType);
static void kissMySaltyAss(); 
static std::vector<std::string> drinkQueue;

static inline std::string getHomeName(std::string name) {
    return name + " Home";
}

// Constants for looking for a cup
// static const float scanning_z = .20;//m
// static const float scanning_r = .50;//m
// static float scanning_speed = -2.5;//rad/s
// static const float scanning_limit_left = 0.0;//rad
// static const float scanning_limit_right = -2 * M_PI/3.0;//rad

// void processCupPoses(const geometry_msgs::PoseArray& cup_positions) {
//     cup_pose_array = cup_positions;
//     last_cup_detection = cup_positions.header.stamp;
//     // std::cout << cup_pose_array << std::endl;
//     // ROS_ERROR("got a cup");
// }

geometry_msgs::Point cup_home;

void processDetections(const bar_bot::Detections& dets) {
    det_positions.clear();
    det_positions[getHomeName(CUP)] = cup_home;
    for (int i = 0; i < (int) dets.detection_types.size(); i++) {
        const std::string& type = dets.detection_types[i];
        det_positions[type] = dets.detection_positions[i];
        // ROS_INFO_STREAM(type << " loc: " << det_positions[type]);
    }
}

// void waitForNewCups() {
//     auto curr = last_cup_detection;
//     while (curr == last_cup_detection) {
//         ros::spinOnce();
//     }
// }

void processArmPose(const geometry_msgs::PoseStamped& arm_pose) {
    last_arm_pos.header = arm_pose.header;
    last_arm_pos.point = arm_pose.pose.position;
}

void processDrinkRequest(const std_msgs::String& drinkType) {
    ROS_INFO("I heard: %s", drinkType.data.c_str());
    if (drinkQueue.size() > 0) {
        ROS_WARN("I already have %d ingredients left, ignoring request", (int) drinkQueue.size());
        return;
    }
    std::string drink = drinkType.data.c_str();
    std::vector<std::string> mixedDrink = det_ingredients[drink];
    drinkQueue = mixedDrink;
    // if(drinkQueue.empty())
    //     std::copy(mixedDrink.begin(), mixedDrink.end(), drinkQueue.end());
    for (const auto& str : drinkQueue) {
        ROS_INFO_STREAM("Ingredients: " << str);
    }
}

static double hypot(double x, double y, double z) {
    return sqrt(x*x + y*y + z*z);
}

static double distance(const geometry_msgs::Point& p1, const geometry_msgs::Point& p2) {
    return hypot(p1.x - p2.x, p1.y - p2.y, p1.z - p2.z);
}

// static geometry_msgs::Point furthestPointFromTip() {
//     auto curr_arm_pos = last_arm_pos;
//     // std::cerr << "last arm pos: " << curr_arm_pos << std::endl;
//     auto curr_cup_poses = cup_pose_array;
//     double max_distance = -1;
//     geometry_msgs::Point best_pos = last_arm_pos.point;
//     if (curr_cup_poses.poses.size() <= 0)
//         ROS_ERROR("NO CUPS FOUND");
//     for (const auto& cup : curr_cup_poses.poses) {
//         // std::cerr << "cup pos: " << cup.position << std::endl;
//         double dist = distance(cup.position, curr_arm_pos.point);
//         if (dist > max_distance) {
//             max_distance = dist;
//             best_pos = cup.position;
//         }
//     }
//     return best_pos;
// }

// Chooses a drink from the queue
std::string popDrink() {
    std::string drink = drinkQueue.front();
    drinkQueue.erase(drinkQueue.begin());
    return drink;
}

void scanForObject(std::string target, bool close_gripper) {
    bar_bot::Mobility mobility;
    float radius = 0.35;
    float height = 0.45;
    float scan_time = 4.0;
    int points = 3;
    float start_theta = -M_PI/4;
    float end_theta =  M_PI/2;

    float step = (end_theta - start_theta)/points;
    float theta =  start_theta;
    while(ros::ok){
        mobility.request.close_gripper      = close_gripper;
        mobility.request.move_time          = scan_time / points; // Seconds
        mobility.request.use_trajectory     = true; // Seconds
        mobility.request.is_blocking        = true;

        mobility.request.target_loc.x  = radius * cos(theta);
        mobility.request.target_loc.y  = radius * sin(theta);
        mobility.request.target_loc.z  = 0.45;
        while(!moveWithFailureChecking(mobility)){
        }
        ros::Duration(0.85).sleep();
        ros::spinOnce();
        if (det_positions.count(target)>0) {
            ROS_INFO_STREAM("Found target: " << target);
            break;
        }
        theta +=step;
        if(theta >= end_theta || theta <= start_theta){
            step *= -1;
        }
    }
}

// Returns where it picked it up
geometry_msgs::Point retrieveDrink(std::string drink) {
    // Move above the cup
    bar_bot::Mobility mobility;
    mobility.request.pour_angle         = 0;
    mobility.request.is_blocking        = true;
    mobility.request.use_trajectory     = true;
    mobility.request.disable_collisions = false;
    static constexpr double height      = .025;

    while(ros::ok()){
        ros::spinOnce();
        if (det_positions.count(drink)>0) {
            mobility.request.target_loc         = det_positions[drink];
            // ROS_INFO_STREAM("coke loc: " << det_positions[drink]);
            mobility.request.close_gripper      = false;
            mobility.request.move_time          = 2.5; // Seconds

            mobility.request.target_loc.x *= .945;
            mobility.request.target_loc.y *= .945;
            mobility.request.target_loc.z += .1;
            if(moveWithFailureChecking(mobility)) {
                break;
            }
        }
    }

    // Wait until we get a new drink detection
    ros::Duration(1).sleep();

    // Move in front of the cup
    ROS_INFO("Move in front of the cup");
    geometry_msgs::Point saved_pos;
    while(ros::ok()){
        ros::spinOnce();
        if (det_positions.count(drink)>0) {
            saved_pos                           = det_positions[drink];
            mobility.request.target_loc         = saved_pos;
            mobility.request.close_gripper      = false;
            mobility.request.move_time          = 1.25; // Seconds

            mobility.request.target_loc.x *= .905;
            mobility.request.target_loc.y *= .905;
            mobility.request.target_loc.z = height;
            if(moveWithFailureChecking(mobility)) {
                break;
            }
        }
    }

    // Move into the cup
    ROS_INFO("Move into the cup");
    while(ros::ok()){
        saved_pos                           = det_positions[drink];
        mobility.request.target_loc         = saved_pos;
        mobility.request.close_gripper      = false;
        mobility.request.move_time          = 1.2; // Seconds
        mobility.request.disable_collisions = true;

        mobility.request.target_loc.x *= 1.025;
        mobility.request.target_loc.y *= 1.025;
        mobility.request.target_loc.z = height;
        if(moveWithFailureChecking(mobility)) {
            break;
        }
    }

    // Grab the cup
    // bar_bot::Mobility mobility;
    ROS_INFO("Grab the cup");
    while(ros::ok()){
        ros::spinOnce();
        mobility.request.target_loc         = saved_pos;
        mobility.request.use_trajectory     = false;
        mobility.request.close_gripper      = true;
        mobility.request.move_time          = 1.2; // Seconds
        mobility.request.disable_collisions = true;

        mobility.request.target_loc.x *= 1.025;
        mobility.request.target_loc.y *= 1.025;
        mobility.request.target_loc.z = height;
        if(moveWithFailureChecking(mobility)) {
            break;
        }
    }

    mobility.request.use_trajectory     = true;

    // back up
    ROS_INFO("back up");
    while(ros::ok()){
        ros::spinOnce();
        mobility.request.target_loc         = saved_pos;
        mobility.request.close_gripper      = true;
        mobility.request.move_time          = 1.25; // Seconds

        mobility.request.target_loc.x *= .8;
        mobility.request.target_loc.y *= .8;
        mobility.request.target_loc.z = height;
        if(moveWithFailureChecking(mobility)) {
            break;
        }
    }

    // Pick up the cup
    ROS_INFO("Pick up the cup");
    while(ros::ok()){
        ros::spinOnce();
        mobility.request.target_loc         = saved_pos;
        mobility.request.close_gripper      = true;
        mobility.request.move_time          = 1.25; // Seconds
        mobility.request.disable_collisions = false;

        mobility.request.target_loc.x *= .8;
        mobility.request.target_loc.y *= .8;
        mobility.request.target_loc.z = 0.25;
        if(moveWithFailureChecking(mobility)) {
            break;
        }
    }
    return saved_pos;
}

void pourIntoTarget(std::string drink) {
    bar_bot::Mobility mobility;
    mobility.request.disable_collisions = false;
    mobility.request.pour_angle         = 0;
    mobility.request.is_blocking        = true;
    mobility.request.use_trajectory     = true;

    const double height = .26;
    const double scale  = .985;

    // Move to above other cup
    while(ros::ok()){
        ros::spinOnce();
        if (det_positions.count(drink)>0) {
            auto target                         = det_positions[drink]; //furthestPointFromTip();
            mobility.request.target_loc         = target;
            mobility.request.close_gripper      = true;
            mobility.request.move_time          = 2.5; // Seconds

            mobility.request.target_loc.x *= scale;
            mobility.request.target_loc.y *= scale;
            mobility.request.target_loc.z = height;
            if(moveWithFailureChecking(mobility)) {
                break;
            }
        } else {
            ROS_INFO_THROTTLE(1, "Can't find drink %s", drink.c_str());
        }
    }

    // Wait until we get a new cup detection
    ros::Duration(1).sleep();

    // Recenter above the cup, and save that as the target to pour over
    geometry_msgs::Point target;
    while(ros::ok()){
        ros::spinOnce();
        if (det_positions.count(drink)>0) {
            target                              = det_positions[drink]; //furthestPointFromTip();
            mobility.request.target_loc         = target;
            mobility.request.close_gripper      = true;
            mobility.request.move_time          = .75; // Seconds

            mobility.request.target_loc.x *= scale;
            mobility.request.target_loc.y *= scale;
            mobility.request.target_loc.z = height;
            if(moveWithFailureChecking(mobility)) {
                break;
            }
        }
    }

    // Pour!
    while(ros::ok()){
        if (det_positions.count(drink)>0) {
            mobility.request.target_loc         = target;
            mobility.request.pour_angle         = M_PI*3.5/4;
            mobility.request.close_gripper      = true;
            mobility.request.move_time          = 7; // Seconds
            mobility.request.pouring_beer       = true;
            mobility.request.beer_nh            = 0.16;
            mobility.request.beer_gh            = 0.05;

            mobility.request.target_loc.x *= scale;
            mobility.request.target_loc.y *= scale;
            mobility.request.target_loc.z = height;
            if(moveWithFailureChecking(mobility)) {
                break;
            }
        }
        ros::spinOnce();
    }
}

void replaceDrink(std::string drink) {
    const static double scale = 1;

    bar_bot::Mobility mobility;
    mobility.request.disable_collisions = false;
    mobility.request.pour_angle         = 0;
    mobility.request.is_blocking        = true;
    mobility.request.use_trajectory     = true;
    mobility.request.pouring_beer       = false;
    mobility.request.beer_nh            = 0;
    mobility.request.beer_gh            = 0;

    std::string home_drink = getHomeName(drink);
    geometry_msgs::Point home;
    while(ros::ok()){
        if (det_positions.count(home_drink)>0) {
            home    = det_positions[home_drink];
            double theta = atan2(home.y, home.x);
            double radius = sqrt(home.x * home.x + home.y * home.y);
            home.x  = (radius - .095) * cos(theta);
            home.y  = (radius - .095) * sin(theta);

            mobility.request.target_loc         = home;
            mobility.request.close_gripper      = true;
            mobility.request.move_time          = 2.5; // Seconds

            mobility.request.target_loc.z = 0.5;
            if(moveWithFailureChecking(mobility)) {
                break;
            }
        }
        ros::spinOnce();
    }

    const double drop_height = 0.06;

    // All the way to the ground
    mobility.request.target_loc         = home;
    mobility.request.close_gripper      = true;
    mobility.request.move_time          = 1.35; // Seconds
    mobility.request.disable_collisions = true;

    mobility.request.target_loc.x *= scale;
    mobility.request.target_loc.y *= scale;
    mobility.request.target_loc.z = drop_height;
    moveWithFailureChecking(mobility);

    ros::Duration(1).sleep(); // Make sure you're really on the ground

    // Release! I said, release boy!
    mobility.request.target_loc         = home;
    mobility.request.close_gripper      = false;
    mobility.request.move_time          = 1.5; // Seconds
    mobility.request.disable_collisions = true;

    mobility.request.target_loc.x *= scale;
    mobility.request.target_loc.y *= scale;
    mobility.request.target_loc.z = drop_height;
    moveWithFailureChecking(mobility);

    ros::Duration(6).sleep();

    // Back it up
    mobility.request.target_loc         = home;
    mobility.request.close_gripper      = false;
    mobility.request.move_time          = 3; // Seconds
    mobility.request.disable_collisions = false;

    mobility.request.target_loc.x *= .8;
    mobility.request.target_loc.y *= .8;
    mobility.request.target_loc.z = 0.1;
    moveWithFailureChecking(mobility);


    // Back it up TO THE SKY BABY
    mobility.request.target_loc         = home;
    mobility.request.close_gripper      = false;
    mobility.request.move_time          = 3; // Seconds
    mobility.request.disable_collisions = false;

    mobility.request.target_loc.x *= .8;
    mobility.request.target_loc.y *= .8;
    mobility.request.target_loc.z = 0.3;
    moveWithFailureChecking(mobility);
}


void pourDrinkIntoCup(std::string drink) {
    scanForObject(drink, false);
    auto loc = retrieveDrink(drink);
    ROS_INFO("drink retrieved: %s", drink.c_str());
    scanForObject(CUP, true);
    // goHomeCup(true);
    pourIntoTarget(CUP);
    ROS_INFO("drink poured into cup: %s", drink.c_str());
    // goHomeBottle(true);
    scanForObject(getHomeName(drink), true);
    replaceDrink(drink);
    ROS_INFO("drink placed: %s", drink.c_str());
    goHomeBottle();
    ROS_INFO("done with %s", drink.c_str());
}

// Sequence to pour a mixed drink
void pourMixedDrink()  {
    // stall until drink request arrives
    while (drinkQueue.size() == 0) {
        ROS_INFO_THROTTLE(1, "Waiting for drink request");
        ros::spinOnce();
    }

    std::string drink;
    while (drinkQueue.size() != 0) {
        drink = popDrink();
        ROS_INFO_STREAM("drink is: " << drink);
        if (drink.compare(SALTYY) == 0) {
            kissMySaltyAss();
        } else {
            pourDrinkIntoCup(drink);
        }
    }
    ros::spinOnce();
}

static bool moveWithFailureChecking(bar_bot::Mobility mobility) {
    mobility.response.target_reached = false;
    mobility_client.call(mobility);
    if (mobility.response.target_reached) {
        ROS_INFO("call successful!");
        return true;
    } else {
        ROS_INFO("call failed!");
        ros::Duration(2).sleep();
        return false;
    }
}

// tODO: goHome for cup and for bottle
void goHomeCup(bool close_gripper) {
    bar_bot::Mobility temp_mobility;
    temp_mobility.request.disable_collisions = false;
    temp_mobility.request.pour_angle         = 0;
    temp_mobility.request.is_blocking        = true;
    temp_mobility.request.use_trajectory     = true;
    temp_mobility.request.close_gripper      = close_gripper;
    temp_mobility.request.move_time          = 4; // Seconds

    temp_mobility.request.target_loc.x = 0;
    temp_mobility.request.target_loc.y = .35;
    temp_mobility.request.target_loc.z = .45;

    ros::spinOnce();
    while (true) {
        temp_mobility.response.target_reached = false;
        mobility_client.call(temp_mobility);
        if(temp_mobility.response.target_reached)
            break;
        ros::Duration(2).sleep();
        ROS_INFO("call failed!");
    }
    ROS_INFO("made it home cup!");
}

// tODO: goHome for cup and for bottle
void goHomeBottle(bool close_gripper) {
    bar_bot::Mobility temp_mobility;
    temp_mobility.request.disable_collisions = false;
    temp_mobility.request.pour_angle         = 0;
    temp_mobility.request.is_blocking        = true;
    temp_mobility.request.use_trajectory     = true;
    temp_mobility.request.close_gripper      = close_gripper;
    temp_mobility.request.move_time          = 4; // Seconds

    temp_mobility.request.target_loc.x = .3;
    temp_mobility.request.target_loc.y = .3;
    temp_mobility.request.target_loc.z = .45;

    ros::spinOnce();
    while (true) {
        temp_mobility.response.target_reached = false;
        mobility_client.call(temp_mobility);
        if(temp_mobility.response.target_reached)
            break;
        ros::Duration(2).sleep();
        ROS_INFO("call failed!");
    }
    ROS_INFO("made it home bottle!");
}

int main(int argc, char **argv) {
    ros::init(argc, argv, "commander_node");
    ros::start();
    ros::Rate loop_rate(5);

    ros::NodeHandle nh;
    last_arm_pos.point.x = .3;
    last_arm_pos.point.y = 0;
    last_arm_pos.point.z = .1;

    end_point.x = -.06;
    end_point.y = .74;
    end_point.z = 0.05;

    detection_topic = "/bar_bot/detections";

    cup_home.x = .0;
    cup_home.y = .75;
    cup_home.z = .1;

    // if (!nh.getParam("cup_detection_topic", cup_detection_topic))
    // {
    //   ROS_ERROR("cup_detection_topic param not specified");
    //   return -1;
    // }
    if (!nh.getParam("target_position_topic", target_position_topic))
    {
      ROS_ERROR("target_position_topic param not specified");
      return -1;
    }
    if (!nh.getParam("target_gripper_state_topic", target_gripper_state_topic))
    {
      ROS_ERROR("target_gripper_state_topic param not specified");
      return -1;
    }
    if (!nh.getParam("current_pose_topic", current_pose_topic))
    {
      ROS_ERROR("current_pose_topic param not specified");
      return -1;
    }

    mobility_client = nh.serviceClient<bar_bot::Mobility>("mobility");

    // target_position_publisher
    //     = nh.advertise<geometry_msgs::PointStamped>(target_position_topic, 1);
    // target_gripper_state_publisher
    //     = nh.advertise<std_msgs::Bool>(target_gripper_state_topic, 1);

    detection_subscriber = nh.subscribe(detection_topic, 10,
        &processDetections);
    current_pose_subscriber = nh.subscribe(current_pose_topic, 10,
        &processArmPose);
    drink_subscriber = nh.subscribe(drink_type_topic, 1,
        &processDrinkRequest);

    goHomeBottle();

    // pourCups();
    // backAndForth();
    // trackCups();
    // pourBeer();
    // pourDrinkIntoCup(SPRITE);
    // pourDrinkIntoCup(GRENADINE);
    // kissMySaltyAss();
    // pourDrinkIntoCup(OJ);
    // // trackCups();

    while(ros::ok()) {
        pourMixedDrink();
        ros::spinOnce();
    }
    // goHomeCup();

    while(ros::ok()) {
        ros::spinOnce();
    }

    ros::shutdown();

    return 0;
}

void trackCups() {
    ros::Rate loop_rate(2);
    bar_bot::Mobility mobility;
    while(ros::ok()){
        if (det_positions.count(CUP)>0) {
            // saved_pos                           = det_positions[CUP];
            mobility.request.target_loc         = det_positions[CUP];
            mobility.request.pour_angle         = 0;
            mobility.request.is_blocking        = true;
            mobility.request.use_trajectory     = true;
            mobility.request.close_gripper      = false;
            mobility.request.move_time          = 1.5; // Seconds
            mobility.request.pouring_beer       = false;

            mobility.request.target_loc.z = 0.25;

            if(moveWithFailureChecking(mobility)) {
            }
        }
        ros::spinOnce();
        loop_rate.sleep();
    }
}

void backAndForth() {
    ros::Rate loop_rate(5);
    // Move back and forth
    bar_bot::Mobility mobility;
    mobility.request.pour_angle         = 0;
    mobility.request.is_blocking        = true;
    mobility.request.use_trajectory     = true;
    mobility.request.close_gripper      = false;
    mobility.request.move_time          = 1.5; // Seconds

    while(ros::ok()){
        ros::spinOnce();
        loop_rate.sleep();
        mobility.request.target_loc.x =  0;
        mobility.request.target_loc.y = .5;
        mobility.request.target_loc.z = .3;

        if(moveWithFailureChecking(mobility)) {
            }

        mobility.request.target_loc.x = .3;
        mobility.request.target_loc.y = .3;
        mobility.request.target_loc.z = .15;

        if(moveWithFailureChecking(mobility)) {
            }
    }
}

void pourBeer() {
    ros::Rate loop_rate(1);
    bar_bot::Mobility mobility;

    // Pour!
    while(ros::ok()){
        mobility.request.pour_angle         = M_PI*3.0/4;
        mobility.request.is_blocking        = true;
        mobility.request.use_trajectory     = true;
        mobility.request.close_gripper      = true;
        mobility.request.move_time          = 5; // Seconds
        mobility.request.pouring_beer       = true;
        mobility.request.beer_nh            = 0.15;
        mobility.request.beer_gh            = 0.05;

        mobility.request.target_loc.x = 0;
        mobility.request.target_loc.y = 0.5;
        mobility.request.target_loc.z = 0.4;

        if(moveWithFailureChecking(mobility)) {
            // Do nothing
        }

        ros::spinOnce();
        loop_rate.sleep();
    }
}

void saltTheCup() {
    bar_bot::Mobility mobility;
    mobility.request.disable_collisions = false;
    mobility.request.pour_angle         = 0;
    mobility.request.is_blocking        = true;
    mobility.request.use_trajectory     = true;

    const double height         = .285;
    const double height_down    = .02;
    const double offset         = .15;

    // Move to above salt plate
    while(ros::ok()){
        ros::spinOnce();
        if (det_positions.count(SALTYY)>0) {
            auto target     = det_positions[SALTYY]; //furthestPointFromTip();
            double theta    = atan2(target.y, target.x);
            double radius   = sqrt(target.x * target.x + target.y * target.y);
            target.x        = (radius - offset) * cos(theta);
            target.y        = (radius - offset) * sin(theta);

            mobility.request.target_loc         = target;
            mobility.request.close_gripper      = true;
            mobility.request.move_time          = 2.5; // Seconds

            mobility.request.target_loc.z = height;
            if(moveWithFailureChecking(mobility)) {
                break;
            }
        }
    }

    // Wait until we get a new cup detection
    ros::Duration(1).sleep();

    // Recenter above the salt plate, and save that as the target to pour over 
    geometry_msgs::Point target;
    while(ros::ok()){
        ros::spinOnce();
        if (det_positions.count(SALTYY)>0) {
            target                              = det_positions[SALTYY]; //furthestPointFromTip();
            double theta    = atan2(target.y, target.x);
            double radius   = sqrt(target.x * target.x + target.y * target.y);
            target.x        = (radius - offset) * cos(theta);
            target.y        = (radius - offset) * sin(theta);
            mobility.request.target_loc         = target;
            mobility.request.close_gripper      = true;
            mobility.request.move_time          = .75; // Seconds

            mobility.request.target_loc.z = height;
            if(moveWithFailureChecking(mobility)) {
                break;
            }
        }
    }

    // Flip the cup upside down
    ROS_INFO("Flip upside down");
    while(ros::ok()){
        mobility.request.target_loc         = target;
        mobility.request.close_gripper      = true;
        mobility.request.move_time          = 2; // Seconds
        mobility.request.pour_angle         = M_PI;
        mobility.request.disable_collisions = true;
        // mobility.request.salting_cup        = true;

        mobility.request.target_loc.z = height;
        if(moveWithFailureChecking(mobility)) {
            break;
        }
        ros::spinOnce();
    }

    ROS_INFO("Touch the ground");
    while(ros::ok()){
        mobility.request.target_loc         = target;
        mobility.request.close_gripper      = true;
        mobility.request.move_time          = 2; // Seconds
        mobility.request.pour_angle         = M_PI;
        mobility.request.disable_collisions = true;
        // mobility.request.salting_cup        = true;

        mobility.request.target_loc.z = height_down;
        if(moveWithFailureChecking(mobility)) {
            break;
        }
        ros::spinOnce();
    }

    // Back up
    ROS_INFO("raise up");
    while(ros::ok()){
        mobility.request.target_loc         = target;
        mobility.request.close_gripper      = true;
        mobility.request.move_time          = 2; // Seconds
        mobility.request.pour_angle         = M_PI;
        mobility.request.disable_collisions = true;
        // mobility.request.salting_cup        = true;

        mobility.request.target_loc.z = height;
        if(moveWithFailureChecking(mobility)) {
            break;
        }
        ros::spinOnce();
    }

    ROS_INFO("unflip");
    while(ros::ok()){
        mobility.request.target_loc         = target;
        mobility.request.close_gripper      = true;
        mobility.request.move_time          = 2; // Seconds
        mobility.request.pour_angle         = 0;
        mobility.request.disable_collisions = true;
        // mobility.request.salting_cup        = true;

        mobility.request.target_loc.z = height;
        if(moveWithFailureChecking(mobility)) {
            break;
        }
        ros::spinOnce();
    }
}

void kissMySaltyAss() {
    scanForObject(CUP, true);
    auto loc_drink = retrieveDrink(CUP);
    scanForObject(SALTYY, true);
    saltTheCup();
    replaceDrink(CUP);
    goHomeCup(false);
}
