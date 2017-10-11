/*
 *  Copyright (c) 2017, Nagoya University
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of Autoware nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "cnn_lidar_detector.hpp"

void CnnLidarDetector::Detect(const cv::Mat& in_depth_image,
                              const cv::Mat& in_height_image,
                              cv::Mat& out_objectness_image,
                              jsk_recognition_msgs::BoundingBoxArray& out_boxes)
{
	caffe::Blob<float>* input_layer = net_->input_blobs()[0];
	input_layer->Reshape(1,
						num_channels_,
						input_geometry_.height,
						input_geometry_.width);
	/* Forward dimension change to all layers. */
	net_->Reshape();

	std::vector<cv::Mat> input_channels;
	WrapInputLayer(&input_channels);//create pointers for input layers

	PreProcess(in_depth_image, in_height_image, &input_channels);

	net_->Forward();

	GetNetworkResults(out_objectness_image, out_boxes);

}

void CnnLidarDetector::get_box_points_from_matrices(size_t row,
                                                    size_t col,
                                                    const std::vector<cv::Mat>& in_boxes_channels,
                                                    CnnLidarDetector::BoundingBoxCorners& out_box)
{
	CHECK_EQ(in_boxes_channels.size(), 24) << "Incorrect Number of points to form a bounding box, expecting 24, got: " << in_boxes_channels.size();

	//bottom layer
	out_box.bottom_front_left.x = in_boxes_channels[0].at<float>(row,col);
	out_box.bottom_front_left.y = in_boxes_channels[1].at<float>(row,col);
	out_box.bottom_front_left.z = in_boxes_channels[2].at<float>(row,col);

	out_box.bottom_back_left.x = in_boxes_channels[3].at<float>(row,col);
	out_box.bottom_back_left.y = in_boxes_channels[4].at<float>(row,col);
	out_box.bottom_back_left.z = in_boxes_channels[5].at<float>(row,col);

	out_box.bottom_back_right.x = in_boxes_channels[6].at<float>(row,col);
	out_box.bottom_back_right.y = in_boxes_channels[7].at<float>(row,col);
	out_box.bottom_back_right.z = in_boxes_channels[8].at<float>(row,col);

	out_box.bottom_front_right.x = in_boxes_channels[9].at<float>(row,col);
	out_box.bottom_front_right.y = in_boxes_channels[10].at<float>(row,col);
	out_box.bottom_front_right.z = in_boxes_channels[11].at<float>(row,col);

	//top layer
	out_box.top_front_left = out_box.bottom_front_left;
	out_box.top_back_left = out_box.bottom_back_left;
	out_box.top_back_right = out_box.bottom_back_right;
	out_box.top_front_right = out_box.bottom_front_right;

	//update only height for the top layer
	out_box.top_front_left.z = in_boxes_channels[14].at<float>(row,col);
	out_box.top_back_left.z = in_boxes_channels[17].at<float>(row,col);
	out_box.top_back_right.z = in_boxes_channels[20].at<float>(row,col);
	out_box.top_front_right.z = in_boxes_channels[23].at<float>(row,col);
}

void CnnLidarDetector::BoundingBoxCornersToJskBoundingBox(const CnnLidarDetector::BoundingBoxCorners& in_box_corners,
                                                          unsigned int in_class,
                                                          std_msgs::Header& in_header,
                                                          jsk_recognition_msgs::BoundingBox& out_jsk_box)
{
	out_jsk_box.header = in_header;

	out_jsk_box.dimensions.x = sqrt(in_box_corners.top_front_left.x * in_box_corners.top_front_left.x -
			                        in_box_corners.top_back_left.x * in_box_corners.top_back_left.x);
	out_jsk_box.dimensions.y = sqrt(in_box_corners.top_front_left.y * in_box_corners.top_front_left.y -
	                                in_box_corners.top_front_right.y * in_box_corners.top_front_right.y);
	out_jsk_box.dimensions.z = in_box_corners.top_front_right.z - in_box_corners.bottom_front_right.z;//any z would do

	//centroid
	out_jsk_box.pose.position.x = (in_box_corners.top_front_left.x + in_box_corners.top_back_right.x) / 2;
	out_jsk_box.pose.position.y = (in_box_corners.top_front_left.y + in_box_corners.top_back_right.y) / 2;
	out_jsk_box.pose.position.z = (in_box_corners.top_front_left.z + in_box_corners.top_front_left.z) / 2;

	//rotation angle
	float x_diff = in_box_corners.top_front_left.x - in_box_corners.top_back_right.x;
	float y_diff = in_box_corners.top_front_left.y - in_box_corners.top_back_right.y;
	float rotation_angle = atan2(y_diff, x_diff);

	tf::Quaternion quat = tf::createQuaternionFromRPY(0.0, 0.0, rotation_angle);
	tf::quaternionTFToMsg(quat, out_jsk_box.pose.orientation);

	out_jsk_box.label = in_class;

}

void CnnLidarDetector::GetNetworkResults(cv::Mat& out_objectness_image,
                                         jsk_recognition_msgs::BoundingBoxArray& out_boxes)
{
	caffe::Blob<float>* boxes_blob = net_->output_blobs().at(0);//0 boxes
	caffe::Blob<float>* objectness_blob = net_->output_blobs().at(1);//1 objectness

	//output layer     0  1    2     3
	//prob 		shape  1 04 height width
	//bb_score 	shape  1 24 height width
	CHECK_EQ(boxes_blob->shape(1), 24) << "The output bb_score layer should be 96 channel image, but instead is " << boxes_blob->shape(1);
	CHECK_EQ(objectness_blob->shape(1), 4) << "The output prob layer should be 4 channel image, but instead is " << objectness_blob->shape(1) ;

	CHECK_EQ(boxes_blob->shape(3), objectness_blob->shape(3)) << "Boxes and Objectness should have the same shape, " << boxes_blob->shape(3);
	CHECK_EQ(boxes_blob->shape(2), objectness_blob->shape(2)) << "Boxes and Objectness should have the same shape, " << boxes_blob->shape(2);

	std::vector<cv::Mat> objectness_channels;
	int width = objectness_blob->shape(3);
	int height = objectness_blob->shape(2);

	//convert objectness (classes) channels to Mat
	float* objectness_ptr = objectness_blob->mutable_cpu_data();//pointer to the prob layer
	//copy each channel(class) from the output layer to a Mat
	for (int i = 0; i < objectness_blob->shape(1); ++i)
	{
		cv::Mat channel(height, width, CV_32FC1, objectness_ptr);
		cv::normalize(channel, channel, 1, 0, cv::NORM_MINMAX);
		objectness_channels.push_back(channel);
		objectness_ptr += width * height;
	}

	//convert boxes (24 floats representing each of the 8 3D points forming the bbox)
	float* boxes_ptr = boxes_blob->mutable_cpu_data();//pointer to the bbox layer
	std::vector<cv::Mat> boxes_channels;
	for (int i = 0; i < boxes_blob->shape(1); ++i)
	{
		cv::Mat channel(height, width, CV_32FC1, boxes_ptr);
		boxes_channels.push_back(channel);
		boxes_ptr += width * height;
	}
	//check each pixel of each channel and assign color depending threshold
	cv::Mat bgr_channels(height, width, CV_8UC3, cv::Scalar(0,0,0));

	std::vector<CnnLidarDetector::BoundingBoxCorners> cars_boxes, person_boxes, bike_boxes;
	for(unsigned int row = 0; row < height; row++)
	{
		for(unsigned int col = 0; col < width; col++)
		{
			//0 nothing
			//1 car, red
			//2 person, green
			//3 bike, blue
			//BGR Image
			CnnLidarDetector::BoundingBoxCorners current_box;
			if (objectness_channels[1].at<float>(row,col) > score_threshold_)
			{
				get_box_points_from_matrices(row, col, boxes_channels, current_box);
				bgr_channels.at<cv::Vec3b>(row,col) = cv::Vec3b(0, 0, 255);
				cars_boxes.push_back(current_box);
			}
			if (objectness_channels[2].at<float>(row,col) > score_threshold_)
			{
				get_box_points_from_matrices(row, col, boxes_channels, current_box);
				bgr_channels.at<cv::Vec3b>(row,col) = cv::Vec3b(0, 255, 0);
				person_boxes.push_back(current_box);
			}
			if (objectness_channels[3].at<float>(row,col) > score_threshold_)
			{
				get_box_points_from_matrices(row, col, boxes_channels, current_box);
				bgr_channels.at<cv::Vec3b>(row,col) = cv::Vec3b(255, 0, 0);
				bike_boxes.push_back(current_box);
			}
		}
	}
	//apply NMS to boxes

	//copy resulting boxes to output message
	out_boxes.boxes.clear();


	cv::flip(bgr_channels, out_objectness_image,-1);
}

void CnnLidarDetector::PreProcess(const cv::Mat& in_depth_image, const cv::Mat& in_height_image, std::vector<cv::Mat>* in_out_channels)
{
	//resize image if required
	cv::Mat depth_resized;
	cv::Mat height_resized;

	if (in_depth_image.size() != input_geometry_)
		cv::resize(in_depth_image, depth_resized, input_geometry_);
	else
		depth_resized = in_depth_image;

	if (in_height_image.size() != input_geometry_)
		{cv::resize(in_height_image, height_resized, input_geometry_);}
	else
		{height_resized = in_height_image;}

	//depth and height images are already preprocessed
	//put each corrected mat geometry onto the correct input layer type pointers
	depth_resized.copyTo(in_out_channels->at(0));
	height_resized.copyTo(in_out_channels->at(1));

	//check that the pre processed and resized mat pointers correspond to the pointers of the input layers
	CHECK(reinterpret_cast<float*>(in_out_channels->at(0).data) == net_->input_blobs()[0]->cpu_data())	<< "Input channels are not wrapping the input layer of the network.";

}

void CnnLidarDetector::WrapInputLayer(std::vector<cv::Mat>* in_out_channels)
{
	caffe::Blob<float>* input_layer = net_->input_blobs()[0];

	int width = input_layer->width();
	int height = input_layer->height();
	float* input_data = input_layer->mutable_cpu_data();
	for (int i = 0; i < input_layer->channels(); ++i)
	{
		cv::Mat channel(height, width, CV_32FC1, input_data);
		in_out_channels->push_back(channel);
		input_data += width * height;
	}
}

CnnLidarDetector::CnnLidarDetector(const std::string& in_network_definition_file,
		const std::string& in_pre_trained_model_file,
		bool in_use_gpu,
		unsigned int in_gpu_id,
		float in_score_threshold)
{
	if(in_use_gpu)
	{
		caffe::Caffe::set_mode(caffe::Caffe::GPU);
		caffe::Caffe::SetDevice(in_gpu_id);
	}
	else
		caffe::Caffe::set_mode(caffe::Caffe::CPU);

	/* Load the network. */
	net_.reset(new caffe::Net<float>(in_network_definition_file, caffe::TEST));
	net_->CopyTrainedLayersFrom(in_pre_trained_model_file);

	caffe::Blob<float>* input_layer = net_->input_blobs()[0];

	num_channels_ = input_layer->channels();

	input_geometry_ = cv::Size(input_layer->width(), input_layer->height());

	score_threshold_ = in_score_threshold;

}
