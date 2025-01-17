/*
 * =====================================================================================
 *
 *       Filename:  vtk_voting_example.cpp
 *
 *    Description:  load pointcloud2 and do sparsevoting
 *                  write back pointcloud2 fields
 *
 *        Version:  1.0
 *        Created:  04/18/2012 07:55 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Ming Liu (), ming.liu@mavt.ethz.ch
 *        Company:  ETHZ
 *
 * =====================================================================================
 */

// system level
#include <iostream>
#include <fstream>
#include <memory>
#include <time.h>
#include <assert.h>

// CUDA related
#include <cuda.h>
#include <helper_math.h>
#include <vector_types.h>

// ROS related
#include "ros/ros.h"
#include "ros/console.h"
#include "pointmatcher/PointMatcher.h"
#include "pointmatcher/IO.h"
#include "pointmatcher_ros/point_cloud.h"
#include "pointmatcher_ros/transform.h"
#include "get_params_from_server.h"

// libpointmatcher
#include "aliases.h"

// libCudaVoting
#include "tensor_voting.h"
#include "CudaVoting.h"

#define USE_GPU_SPARSE_BALL

using namespace PointMatcherSupport;
using namespace std;
using namespace topomap;
using namespace cudavoting;

//! A dense matrix over ScalarType
typedef Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic> Matrix;
//! A dense integer matrix
typedef Eigen::Matrix<int, Eigen::Dynamic, Eigen::Dynamic> IntMatrix;

class SparseVotingCloudGPU
{
    ros::NodeHandle & n;
    // subscriber
	ros::Subscriber cloudSub;
	string cloudTopicIn;
    // publisher
	ros::Publisher cloudPub;
	string cloudTopicOut;

	DP cloud;

    // tensor voting
    float sigma; // sparse voting kernel size

    // parameter
    bool savevtk; // whether save sequence of vtk files.
	const string mapFrame;

public:
	SparseVotingCloudGPU(ros::NodeHandle& n);
	void gotCloud(const sensor_msgs::PointCloud2& cloudMsg);
    void publish(); // publish tensor voting result
    void process(DP & cloud, float sigma);
};

SparseVotingCloudGPU::SparseVotingCloudGPU(ros::NodeHandle& n):
    n(n),
	mapFrame(getParam<string>("mapFrameId", "/map"))
{
	// ROS initialization
    sigma = getParam<double>("sigma", 1.0);
	cloudTopicIn = getParam<string>("cloudTopicIn", "/point_cloud");
	cloudTopicOut = getParam<string>("cloudTopicOut", "/point_cloud_out");
    savevtk = getParam<bool>("savevtk", false);

	cloudSub = n.subscribe(cloudTopicIn, 100, &SparseVotingCloudGPU::gotCloud, this);
	cloudPub = n.advertise<sensor_msgs::PointCloud2>(
		getParam<string>("cloudTopicOut", "/point_cloud_sparsevoting"), 1
	);
}


void SparseVotingCloudGPU::gotCloud(const sensor_msgs::PointCloud2& cloudMsgIn)
{
    cloud = DP(PointMatcher_ros::rosMsgToPointMatcherCloud<float>(cloudMsgIn));

    // do sparse tensor voting
    process(cloud, sigma);

    ROS_INFO("output Cloud descriptor pointcloud size: %d", 
                (unsigned int)(cloud.features.cols()));

    // publishing
    publish();

    if(savevtk)
    {
	    stringstream nameStream;
	    nameStream << "." << cloudTopicIn << "_" << cloudMsgIn.header.seq;
	    PointMatcherIO<float>::saveVTK(cloud, nameStream.str());
    }
}

void SparseVotingCloudGPU::publish()
{
	if (cloudPub.getNumSubscribers())
		cloudPub.publish(PointMatcher_ros::pointMatcherCloudToRosMsg<float>(cloud, mapFrame, ros::Time::now()));
}

void SparseVotingCloudGPU::process(DP & cloud, float sigma)
{
    unsigned int numPoints = cloud.features.size()/4;
    cout << "Input size: " << numPoints << endl;
    cout << "Sparse ball voting (GPU)..." << endl;

    // 1. allocate field
    Eigen::Matrix<Eigen::Matrix3f, Eigen::Dynamic, 1> sparseField;
    size_t sizeField = numPoints*3*sizeof(float3);
    float3* h_fieldarray = (float3 *)malloc(sizeField);

    // 2. allocate points
    size_t sizePoints = numPoints*sizeof(float3);
    float3 *h_points = (float3 *)malloc(sizePoints);
    for(unsigned int i = 0; i<numPoints; i++)
    {
        h_points[i].x = cloud.features(0,i); 
        h_points[i].y = cloud.features(1,i); 
        h_points[i].z = cloud.features(2,i); 
    }
    // 3. set log
    size_t sizeLog = numPoints*sizeof(int2);
    int2 * h_log = (int2 *)malloc(sizeLog);
    bzero( h_log, sizeLog);

    // 4. call CUDA
    cout << "Send to GPU..." << endl;
    CudaVoting::sparseBallVoting(h_fieldarray, h_points, sigma, numPoints, h_log);

    // 5. post-processing
    sparseField.resize(numPoints, 1);
    for(unsigned int i = 0; i<numPoints; i++)
    {
        Eigen::Matrix3f M;
        M << h_fieldarray[i*3 + 0].x, h_fieldarray[i*3 + 0].y, h_fieldarray[i*3 + 0].z, 
             h_fieldarray[i*3 + 1].x, h_fieldarray[i*3 + 1].y, h_fieldarray[i*3 + 1].z, 
             h_fieldarray[i*3 + 2].x, h_fieldarray[i*3 + 2].y, h_fieldarray[i*3 + 2].z;
        sparseField(i) = M;
    }

    // 6. Split on GPU:
    cout	<< "sparse tensor split..." << endl;
    size_t sizeSaliency = numPoints*sizeof(float);
    float * h_stick = (float*)malloc(sizeSaliency);
    float * h_plate = (float*)malloc(sizeSaliency);
    float * h_ball = (float*)malloc(sizeSaliency);
    // sparse fields
    float3 * h_sparse_stick_field = (float3 *)malloc(numPoints*sizeof(float3));
    float3 * h_sparse_plate_field = (float3 *)malloc(numPoints*sizeof(float3));

    CudaVoting::tensorSplitWithField(h_fieldarray, h_stick, h_plate, h_ball, 
                h_sparse_stick_field, h_sparse_plate_field, numPoints);


    // 7. save sparse tensor
    PointMatcher<float>::Matrix stick=PM::Matrix::Zero(1, numPoints);
    PointMatcher<float>::Matrix plate=PM::Matrix::Zero(1, numPoints);
    PointMatcher<float>::Matrix ball =PM::Matrix::Zero(1, numPoints);
    PointMatcher<float>::Matrix normals=PM::Matrix::Zero(3, numPoints);
    for(unsigned int i=0; i<numPoints; i++)
    {
        stick(i) = h_stick[i];
        plate(i) = h_plate[i];
        ball(i) =  h_ball[i];
        normals.col(i) << h_sparse_stick_field[i].x,h_sparse_stick_field[i].y,h_sparse_stick_field[i].z;
    }
    cloud.addDescriptor("stick", stick);
    cloud.addDescriptor("plate", plate);
    cloud.addDescriptor("ball", ball);
    cloud.addDescriptor("normals", normals);

    // 8. clean up
    free(h_fieldarray);
    free(h_points);
    free(h_log);
    free(h_stick);
    free(h_plate);
    free(h_ball);
    free(h_sparse_stick_field);
    free(h_sparse_plate_field);
}


// Main function supporting the SparseVotingCloudGPU class
int main(int argc, char **argv)
{
	ros::init(argc, argv, "demo_sparse_gpu");
	ros::NodeHandle n;
	SparseVotingCloudGPU gpuSparseVoter(n);
	ros::spin();
	
	return 0;
}
