/***************************************************************************
 *   Copyright (C) 2006 - 2017 by                                          *
 *      Tarek Taha, KURI  <tataha@tarektaha.com>                           *
 *      Randa Almadhoun   <randa.almadhoun@kustar.ac.ae>                   *
 *                                                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Steet, Fifth Floor, Boston, MA  02111-1307, USA.          *
 ***************************************************************************/
#include <victim_localization/reactive_planner_server.h>
ReactivePlannerServer::ReactivePlannerServer(const ros::NodeHandle& nh_, const ros::NodeHandle& nh_private_, volumetric_mapping::OctomapManager *mapManager_):
    nh(nh_),
    nh_private(nh_private_),
    mapManager(mapManager_)
{
    // sub = nh.subscribe<sensor_msgs::PointCloud2>("cloud_pcd", 1, &ReactivePlannerServer::callback,this);
    //planningService = nh_private.advertiseService("sspp_planner", &ReactivePlannerServer::plannerCallback, this);
    visualTools.reset(new rviz_visual_tools::RvizVisualTools("world", "/sspp_visualisation"));
    visualTools->loadMarkerPub();
    gridStart.position.x = 0.0;
    gridStart.position.y = 0.0;
    gridStart.position.z = 0.0;

    getConfigsFromRosParams();
    visualTools->deleteAllMarkers();
    visualTools->enableBatchPublishing();
    ROS_INFO("Starting the reactive planning");

    ros::param::param<double>("~tolerance_dist_to_robot",tolerance_dist_to_robot,1.0);
    ros::param::param<bool>("~enable_straight_line_check",enable_straight_line_check,true);
}

ReactivePlannerServer::~ReactivePlannerServer()
{
    if(robot)
        delete robot;
    if(pathPlanner)
        delete pathPlanner;
    /*
  if (mapManager)
    delete manager;
  */
}

bool ReactivePlannerServer::PathGeneration(geometry_msgs::Pose start_, geometry_msgs::Pose end_,nav_msgs::Path &path_)
{
    geometry_msgs::PoseArray pathPoses;

    if (!PathGeneration(start_,end_,pathPoses))
        return false;

    // setting the path
    geometry_msgs::PoseStamped ps;
    ps.header.frame_id="world";
    ps.header.stamp= ros::Time::now();
    for (int i=0; i<pathPoses.poses.size(); i++) {
        ps.pose = pathPoses.poses[i];
        path_.poses.push_back(ps);
    }

    // transfere the orientation for the end pose to the end pose in the path
    geometry_msgs::PoseStamped end_ps;
    end_ps = path_.poses.back();
    end_ps.pose.orientation = end_.orientation;
    path_.poses.erase(path_.poses.end());
    path_.poses.insert(path_.poses.end(),end_ps);

    return true;
}

bool ReactivePlannerServer::PathGeneration(geometry_msgs::Pose start_, geometry_msgs::Pose end_,std::vector<geometry_msgs::Pose> &path_)
{
    geometry_msgs::PoseArray pathPoses;

    if (!PathGeneration(start_,end_,pathPoses))
        return false;

    // setting the path
    path_=pathPoses.poses;

    // transfere the orientation for the end pose to the end pose in the path
    geometry_msgs::Pose end_p;
    end_p = path_.back();
    end_p.orientation = end_.orientation;
    path_.erase(path_.end());
    path_.insert(path_.end(),end_p);

    return true;
}

bool ReactivePlannerServer::PathGeneration(geometry_msgs::Pose start_, geometry_msgs::Pose end_,geometry_msgs::PoseArray &pathPoses)
{
    mapManager->getAllOccupiedBoxes(&occupied_box_vector);
    ROS_INFO_THROTTLE(1,"Received a Service Request Planning MAP SIZE:[%f %f %f] occupied cells:%lu",mapManager->getMapSize()[0],mapManager->getMapSize()[1],mapManager->getMapSize()[2],occupied_box_vector.size());

    if(occupied_box_vector.size()<=0 || mapManager->getMapSize().norm() <= 0.0)
    {
        ROS_INFO_THROTTLE(1, "Planner not set up: Octomap is empty!");
        return false;
    }

    visualTools->deleteAllMarkers(); // clear the markers


    ros::Time timer_start = ros::Time::now();

    start.p   = start_;
    end.p     = end_;

    dist_robot_to_goal=sqrt(pow(start.p.position.x-end.p.position.x,2)+pow(start.p.position.y-end.p.position.y,2));

    if (dist_robot_to_goal<distanceToGoal)
    {
        std::cout << cc.red << "The robot is close with a distance less that dist_to_goal... pathplanner terminating...\n" << cc.reset;
        return false;
    }

    std::cout << start.p << std::endl;
    std::cout << end.p << std::endl;

    visualTools->publishSphere(start.p, rviz_visual_tools::BLUE, 0.3,"start_pose");
    visualTools->publishSphere(end.p, rviz_visual_tools::ORANGE, 0.3,"end_pose");
    visualTools->trigger();

    double robotH = 0.2, robotW = 0.4, narrowestPath = 0.5;
    //narrowestPath = 0.987;

    robotCenter.x = 0.0f;
    //robotCenter.x = -0.3f;
    robotCenter.y = 0.0f;
    if(!robot)
        robot = new Robot("Robot", robotH, robotW, narrowestPath, robotCenter);

    //Every how many iterations to display the tree; -1 disable display
    if(!pathPlanner)
        pathPlanner = new SSPP::PathPlanner(nh, robot, regGridConRad, treeProgressDisplayFrequency);

    // This causes the planner to pause for the desired amount of time and display
    // the search tree, useful for debugging
    pathPlanner->setDebugDelay(debugDelay);

    SSPP::DistanceHeuristic distanceHeuristic(nh, debug,mapManager,visualTools);
    distanceHeuristic.setEndPose(end.p);
    distanceHeuristic.setTolerance2Goal(distanceToGoal);
    pathPlanner->setHeuristicFucntion(&distanceHeuristic);

    // Generate Grid Samples and visualise it

    std::cout << "input map data as follow" << std::endl;
    std::cout << "start" << start.p << std::endl;
    std::cout << "end" << end.p << std::endl;
    std::cout << "gridstart" << gridStart << std::endl;
    std::cout << "size"<< gridSize << std::endl;
    std::cout << "res" << gridRes << std::endl;
    std::cout << "sampleOrien" << sampleOrientations << std::endl;
    std::cout << "sampleOrienReso"<< orientationSamplingRes << std::endl;

    pathPlanner->generateRegularGrid(gridStart, gridSize, gridRes, sampleOrientations, orientationSamplingRes,false, true);
    std::vector<geometry_msgs::Point> searchSpaceNodes = pathPlanner->getSearchSpace();

    std::cout << "\n\n---->>> Total Nodes in search Space ="<< searchSpaceNodes.size();

    std::vector<geometry_msgs::PoseArray> sensorsPoseSS;
    geometry_msgs::PoseArray robotPoseSS;
    pathPlanner->getRobotSensorPoses(robotPoseSS, sensorsPoseSS);
    std::cout << "\n\n---->>> Total Nodes in search Space ="<< searchSpaceNodes.size();

    visualTools->publishSpheres(searchSpaceNodes, rviz_visual_tools::PURPLE, 0.1,"search_space_nodes");
    visualTools->trigger();

    // Connect nodes and visualise it
    pathPlanner->connectNodes();
    std::cout << "\nSpace Generation took:"
              << double(ros::Time::now().toSec() - timer_start.toSec())
              << " secs";
    if(visualizeSearchSpace)
    {
        std::vector<geometry_msgs::Point> searchSpaceConnections = pathPlanner->getConnections();
        for(int i =0; i<(searchSpaceConnections.size() - 1) ;i+=2)
        {
            visualTools->publishLine(searchSpaceConnections[i], searchSpaceConnections[i+1], rviz_visual_tools::BLUE,rviz_visual_tools::LARGE);
        }
        visualTools->trigger();
    }

    std::cout << "searching for the best link\n";

    // Find path and visualise it
    ros::Time timer_restart = ros::Time::now();
    SSPP::Node* path = pathPlanner->startSearch(start);
    ros::Time timer_end = ros::Time::now();
    std::cout << "\nPath Finding took:" << double(timer_end.toSec() - timer_restart.toSec()) << " secs";

    if (path)
    {
        pathPlanner->printNodeList();
    }
    else
    {
        std::cout << "\nNo Path Found";
        double previous_distance_to_goal = distanceToGoal;
        double distanceToGoal=distanceToGoal+regGridConRad;
        if (distanceToGoal>(dist_robot_to_goal-tolerance_dist_to_robot))
        {
            std::cout << cc.green << "dis_to_robot less than tolerance.. terminating.." << cc.reset;
            return false;
        }

        if(enable_straight_line_check)
        {
            while (distanceToGoal<=(dist_robot_to_goal-tolerance_dist_to_robot))
            {
                // try a discending straight-line
                std::cout << "new distance to goal is...." << distanceToGoal << std::endl;
                distanceHeuristic.setTolerance2Goal(distanceToGoal);
                pathPlanner->setHeuristicFucntion(&distanceHeuristic);
                path = pathPlanner->startSearch(start);
                ros::Time timer_end = ros::Time::now();
                std::cout << "\nPath Finding took:" << double(timer_end.toSec() - timer_restart.toSec()) << " secs";
                if (path)
                {
                    pathPlanner->printNodeList();
                    std::cout << "Found path over a descending line with dist_to_goal.." << distanceToGoal << std::endl;
                    break;
                }
                else
                {
                    // if failed , then try bigger distanct to goal
                    distanceToGoal+=regGridConRad;
                }
            }
        }

        else
        {
           // check for distance_to_goal=dist_robot_to_goal-tolerance_dist_to_robot
            distanceToGoal=dist_robot_to_goal-tolerance_dist_to_robot;

            std::cout << "new distance to goal using Tolerance...." << distanceToGoal << std::endl;
            distanceHeuristic.setTolerance2Goal(distanceToGoal);
            pathPlanner->setHeuristicFucntion(&distanceHeuristic);
            path = pathPlanner->startSearch(start);
            ros::Time timer_end = ros::Time::now();
            std::cout << "\nPath Finding took:" << double(timer_end.toSec() - timer_restart.toSec()) << " secs";
            if (path)
            {
                pathPlanner->printNodeList();
                std::cout << "Found path over a descending line with dist_to_goal.." << distanceToGoal << std::endl;
            }
        }
        //return the original distance to goal
        distanceToGoal=previous_distance_to_goal;
    }

    if (!path)
    {
    std::cout << cc.blue << "failed to found path with the new distance to goal...terminaing....\n" << cc.reset;
    return false;
    }

    geometry_msgs::Point linePoint;
    std::vector<geometry_msgs::Point> pathSegments;
    geometry_msgs::PoseArray robotPose, sensorPose;
    double dist = 0;
    double yaw;
    while (path != NULL)
    {
        tf::Quaternion qt(path->pose.p.orientation.x, path->pose.p.orientation.y,
                          path->pose.p.orientation.z, path->pose.p.orientation.w);
        yaw = tf::getYaw(qt);
        pathPoses.poses.push_back(path->pose.p);
        if (path->next != NULL)
        {
            linePoint.x = path->pose.p.position.x;
            linePoint.y = path->pose.p.position.y;
            linePoint.z = path->pose.p.position.z;
            robotPose.poses.push_back(path->pose.p);
            for (int i = 0; i < path->senPoses.size(); i++)
                sensorPose.poses.push_back(path->senPoses[i].p);
            pathSegments.push_back(linePoint);

            linePoint.x = path->next->pose.p.position.x;
            linePoint.y = path->next->pose.p.position.y;
            linePoint.z = path->next->pose.p.position.z;
            robotPose.poses.push_back(path->next->pose.p);
            for (int i = 0; i < path->next->senPoses.size(); i++)
                sensorPose.poses.push_back(path->next->senPoses[i].p);
            pathSegments.push_back(linePoint);

            dist = dist + Dist(path->next->pose.p, path->pose.p);
        }
        path = path->next;
    }

    std::cout << "\nDistance calculated from the path: " << dist << "m\n";

    if (dist==0)
    {
        ROS_WARN("The Robot is already in the target pose.... The path generation is set to success... ");
        return false;
    }

    for(int i =0; i<(pathSegments.size() - 1) ;i++)
    {
        visualTools->publishLine(pathSegments[i], pathSegments[i+1],rviz_visual_tools::RED,rviz_visual_tools::XLARGE);
    }
    visualTools->trigger();

    for (int i = 0; i < robotPose.poses.size(); i++)
    {
        visualTools->publishArrow(robotPose.poses[i], rviz_visual_tools::YELLOW, rviz_visual_tools::LARGE, 0.3);
    }
    visualTools->trigger();

    for (int i = 0; i < robotPoseSS.poses.size(); i++)
    {
        visualTools->publishArrow(robotPoseSS.poses[i], rviz_visual_tools::CYAN, rviz_visual_tools::LARGE, 0.3);
    }
    visualTools->trigger();

    for (int i = 0; i < sensorsPoseSS.size(); i++)
    {
        for(int j = 0; j < sensorsPoseSS[i].poses.size(); j++)
            visualTools->publishArrow(sensorsPoseSS[i].poses[j], rviz_visual_tools::DARK_GREY, rviz_visual_tools::LARGE, 0.3);
    }
    visualTools->trigger();
    return true;
}

void ReactivePlannerServer::getConfigsFromRosParams()
{
    std::string ns = ros::this_node::getName();
    std::cout<<"Node name is:"<<ns<<"\n";
    nh_private.param("start_x",start.p.position.x,start.p.position.x);
    nh_private.param("start_y",start.p.position.y,start.p.position.y);
    nh_private.param("start_z",start.p.position.z,start.p.position.z);
    nh_private.param("end_x",end.p.position.x,end.p.position.x);
    nh_private.param("end_y",end.p.position.y,end.p.position.y);
    nh_private.param("end_z",end.p.position.z,end.p.position.z);
    nh_private.param("connection_rad",regGridConRad,regGridConRad);
    nh_private.param("grid_resolution",gridRes,gridRes);
    nh_private.param("grid_size_x",gridSize.x,gridSize.x);
    nh_private.param("grid_size_y",gridSize.y,gridSize.y);
    nh_private.param("grid_size_z",gridSize.z,gridSize.z);
    nh_private.param("grid_start_x",gridStart.position.x,gridStart.position.x);
    nh_private.param("grid_start_y",gridStart.position.y,gridStart.position.y);
    nh_private.param("grid_start_z",gridStart.position.z,gridStart.position.z);
    nh_private.param("visualize_search_space",visualizeSearchSpace,visualizeSearchSpace);
    nh_private.param("debug",debug,debug);
    nh_private.param("debug_delay",debugDelay,debugDelay);
    nh_private.param("dist_to_goal",distanceToGoal,distanceToGoal);
    nh_private.param("sample_orientations",sampleOrientations,sampleOrientations);
    nh_private.param("orientation_sampling_res",orientationSamplingRes,orientationSamplingRes);
    nh_private.param("tree_progress_display_freq",treeProgressDisplayFrequency,treeProgressDisplayFrequency);
}

void ReactivePlannerServer::SetDynamicGridSize(double x, double y, double z)
{
    gridSize.x=x;
    gridSize.y=y;
    gridSize.z=z;
}

void ReactivePlannerServer::SetOriginPose(double x, double y, double z)
{
    gridStart.position.x=x;
    gridStart.position.y=y;
    gridStart.position.z=z;
}
