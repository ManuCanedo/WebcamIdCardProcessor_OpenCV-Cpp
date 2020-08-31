#include "stdafx.h"
#include "opencv/cv.h"
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include "opencv2/objdetect/objdetect.hpp"
#include "opencv2/video/video.hpp"
#include "opencv2/xfeatures2d.hpp"
#include <iostream>
#include <stdio.h>
#include <string>
#include <sstream>
#include <math.h>
#include <vector>
#include "opencv2/features2d.hpp"
#include "opencv2/calib3d.hpp"

//Namespaces
using namespace cv;
using namespace cv::xfeatures2d;

//Global variables
vector<Mat> segmentedInfo;
Mat dniROI;
String face_cascade_name = "haarcascade_frontalface_alt.xml";
String eyes_cascade_name = "haarcascade_eye_tree_eyeglasses.xml";
CascadeClassifier face_cascade;
CascadeClassifier eyes_cascade;

//Function declarations
Mat MRZExtraction(Mat image);
void faceDetection(Mat img);
void infoExtractor(Mat img);
void launchProcessing(Mat ROI, bool front);
bool homographyDetector(Mat frame, Mat templ);

int main(int argc, char **argv)
{
	//-- 1. Load the cascades
	if (!face_cascade.load(face_cascade_name))
	{
		printf("--(!)Error loading\n");
		return -1;
	};
	if (!eyes_cascade.load(eyes_cascade_name))
	{
		printf("--(!)Error loading\n");
		return -1;
	};

	//-- 1.temp load DNI for testing
	Mat templ1 = cv::imread(argv[1]);

	//-- 2. Read the video stream
	Mat frame;
	string arg = "";

	VideoCapture capture(arg); //try to open string, this will attempt to open it as a video file
	if (!capture.isOpened())   //if this fails, try to open as a video camera, through the use of an integer param
		capture.open(atoi(arg.c_str()));
	if (!capture.isOpened())
	{
		cerr << "Failed to open a video device or video file!\n"
			 << endl;
		return 1;
	}

	Ptr<BackgroundSubtractorMOG2> bgModel;
	bgModel = createBackgroundSubtractorMOG2(300, 16, false); // construct the class for background subtraction

	for (;;)
	{
		capture >> frame;
		if (!frame.empty())
		{
			imshow("Input", frame);
			if (homographyDetector(frame, templ1))
			{
				launchProcessing(dniROI, true);
			}
		}
		else
		{
			printf(" --(!) No captured frame -- Break!");
			break;
		}

		char key = static_cast<char>(waitKey(10)); //delay N millis, usually long enough to display and capture input
		switch (key)
		{
		case 'b':
			try
			{
				Mat img = cv::imread(argv[2]);
				launchProcessing(img, false);
			}
			catch (cv::Exception &e)
			{
				printf("No es posible realizar esta acción sobre la imagen seleccionada");
			}
			break;
		case 'f':
			try
			{
				Mat img = cv::imread(argv[2]);
				launchProcessing(img, true);
			}
			catch (cv::Exception &e)
			{
				printf("No es posible realizar esta acción sobre la imagen seleccionada");
			}
			break;
		case 27: //escape key
			return 0;
		default:
			break;
		}
	}
	return 0;
}

void infoExtractor(Mat img)
{

	Mat showie, rgb, small, grad, bw, connected;

	// Blurs an image and downsamples it.
	pyrDown(img, rgb);
	cvtColor(rgb, small, CV_BGR2GRAY);

	// morphological gradient
	Mat morphKernel = getStructuringElement(MORPH_ELLIPSE, Size(3, 3));
	morphologyEx(small, grad, MORPH_GRADIENT, morphKernel);

	// binarize
	threshold(grad, bw, 0.0, 255.0, THRESH_BINARY | THRESH_OTSU);

	// connect horizontally oriented regions with CLOSING
	morphKernel = getStructuringElement(MORPH_RECT, Size(7, 2));
	morphologyEx(bw, connected, MORPH_CLOSE, morphKernel);

	// find contours
	Mat mask = Mat::zeros(bw.size(), CV_8UC1);
	vector<vector<Point>> contours;
	vector<Vec4i> hierarchy;
	findContours(connected, contours, hierarchy, CV_RETR_CCOMP, CV_CHAIN_APPROX_SIMPLE, Point(0, 0));

	// filter contours
	for (int idx = 0; idx >= 0; idx = hierarchy[idx][0])
	{
		Rect rect = boundingRect(contours[idx]);
		Mat maskROI(mask, rect);
		maskROI = Scalar(0, 0, 0);

		// fill the contour
		drawContours(mask, contours, idx, Scalar(255, 255, 255), CV_FILLED);

		// ratio of non-zero pixels in the filled region
		double r = (double)countNonZero(maskROI) / (rect.width * rect.height);

		// assume at least 25% of the area is filled if it contains text
		if (r > .25 && (rect.height > 20 && rect.width > 60)) // constraints on region size
		{
			showie = Mat(rgb, rect);
			segmentedInfo.push_back(showie);
			//rectangle(rgb, rect, Scalar(0, 255, 0), 2);
		}
	}
}

void faceDetection(Mat img)
{

	Mat img_gray, faceROI, descriptorsFace, descriptorsFrame;
	Rect rectFace;
	vector<Rect> faces1;
	vector<KeyPoint> keypointsFace, keypointsFrame;
	const int MAX_COUNT = 500, MAX_ITER = 10;
	int morph_size = 5;
	bool trackObject = false;

	cvtColor(img, img_gray, CV_BGR2GRAY); //Works better with gray images
	equalizeHist(img_gray, img_gray);
	face_cascade.detectMultiScale(img_gray, faces1, 1.1, 2, 0 | CV_HAAR_SCALE_IMAGE, Size(3, 3)); //used to detect the face

	int max_face_width = 0, index;
	if (faces1.size() > 0)
	{
		trackObject = true;
		for (size_t i = 0; i < faces1.size(); i++)
		{
			if (faces1[i].width > max_face_width)
			{
				max_face_width = faces1[i].width;
				index = i;
			}
		}
		int h_temp = faces1[index].height; 			// storing original height
		int w_temp = faces1[index].width;
		int x = faces1[index].x - w_temp * 0.15; 	//x is reduced by 0.15*x
		int y = faces1[index].y - h_temp * 0.33; 	// y is reduced by 0.33*h
		int w = w_temp * 1.3, h = h_temp * 1.57;	// height is increases by 57%
		rectFace = Rect(x, y, w, h); 				// We create a rectangle with the coordinates of the face
		Mat result = img(rectFace);

		segmentedInfo.push_back(result);
	}
}

Mat MRZExtraction(Mat image)
{

	//Gradiantes used as a container of Sobel's output
	Mat gradX, thresh, gray, blackhat;
	Mat rectKernel = getStructuringElement(MORPH_RECT, Size(13, 5), Point(0, 0));
	Mat sqKernel = getStructuringElement(MORPH_RECT, Size(30, 30), Point(0, 0));
	float ar; //aspect ratio

	resize(image, image, Size(image.cols, 600));
	cvtColor(image, gray, COLOR_BGR2GRAY);

	// smooth the image using a 3x3 Gaussian, then apply the blackhat
	// morphological operator to find dark regions on a light background
	GaussianBlur(gray, gray, Size(3, 3), 0);
	morphologyEx(gray, blackhat, MORPH_BLACKHAT, rectKernel);

	// compute the Scharr gradient of the blackhat image and scale the
	// result into the range[0, 255]
	Sobel(gray, gradX, CV_32F, 1, 0, -1);
	gradX = abs(gradX);
	double minVal, maxVal;
	minMaxLoc(gradX, &minVal, &maxVal);
	gradX = (255 * ((gradX - minVal) / (maxVal - minVal)));
	gradX.convertTo(gradX, CV_8U);

	// apply a closing operation using the rectangular kernel to close
	// gaps in between letters -- then apply Otsu's thresholding method-
	morphologyEx(gradX, gradX, MORPH_CLOSE, rectKernel);
	threshold(gradX, thresh, 0, 255, THRESH_OTSU);

	// perform another closing operation, this time using the square
	// kernel to close gaps between lines of the MRZ, then perform a
	// series of erosions to break apart connected components
	morphologyEx(thresh, thresh, MORPH_CLOSE, sqKernel);
	erode(thresh, thresh, Mat(), Point(-1, 1), 4);

	//Find conoturs in the thresholded imge and sort them by their size
	std::vector<std::vector<cv::Point>> contours; // We need to use the std namespace to sort the contours properly
	findContours(thresh, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

	int pY, pX;
	int x, y, w, h;
	Mat roi;

	for (int c = 0; c < contours.size(); c++)
	{
		// compute the bounding box of the contour and use the contour to
		// compute the aspect ratio and coverage ratio of the bounding box
		// width to the width of the image
		vector<Rect> boundRect(contours.size());
		boundRect[c] = boundingRect(contours[c]);
		ar = boundRect[c].width / boundRect[c].height;
		if (ar > 5 && ar < 10)
		{
			// pad the bounding box since we applied erosions and now need
			// to re - grow it
			pX = (boundRect[c].x + boundRect[c].width) * 0.03;
			pY = (boundRect[c].y + boundRect[c].height) * 0.03;
			x = boundRect[c].x - pX;
			y = boundRect[c].y - pY;
			w = boundRect[c].width + (pX * 2);
			h = boundRect[c].height + (pY * 2);
			/*    extract the ROI from the image and draw a bounding box
			surrounding the MRZ */
			// Setup a rectangle to define your region of interest
			roi = image(Rect(x, y, w, h));
			rectangle(image, Point(x, y), Point(x + w, y + h), Scalar(0, 255, 0), 2);
			break;
		}
	}
	return roi;
}

void launchProcessing(Mat ROI, bool front)
{
	segmentedInfo.clear();
	if (front)
	{
		faceDetection(ROI);
		infoExtractor(ROI);
		string named;
		for (int i = 0; i < segmentedInfo.size(); i++)
		{
			stringstream ss;
			ss << i + 1;
			named = "field nº " + ss.str();
			namedWindow(named, WINDOW_NORMAL);
			imshow(named, segmentedInfo[i]);
		}
		waitKey(0);
	}
	else
	{
		Mat MRZ = MRZExtraction(ROI);
		namedWindow("MRZ", WINDOW_NORMAL);
		imshow("MRZ", MRZ);
		waitKey(0);
	}
}

bool homographyDetector(Mat frame, Mat referencia)
{
	if (!referencia.data || !frame.data)
	{
		std::cout << " --(!) Error reading images " << std::endl;
		return -1;
	}

	//-- Step 1: Detect the keypoints and extract descriptors using SURF
	int minHessian = 400;
	Ptr<SURF> detector = SURF::create(minHessian);
	std::vector<KeyPoint> keypoints_object, keypoints_scene;
	Mat descriptors_object, descriptors_scene;
	detector->detectAndCompute(referencia, Mat(), keypoints_object, descriptors_object);
	detector->detectAndCompute(frame, Mat(), keypoints_scene, descriptors_scene);

	//-- Step 2: Matching descriptor vectors using FLANN matcher
	FlannBasedMatcher matcher;
	std::vector<DMatch> matches;
	matcher.match(descriptors_object, descriptors_scene, matches);
	double max_dist = 0;
	double min_dist = 100;

	//-- Quick calculation of max and min distances between keypoints
	for (int i = 0; i < descriptors_object.rows; i++)
	{
		double dist = matches[i].distance;
		if (dist < min_dist)
			min_dist = dist;
		if (dist > max_dist)
			max_dist = dist;
	}

	//-- Draw only "good" matches (i.e. whose distance is less than 3*min_dist )
	std::vector<DMatch> good_matches;
	for (int i = 0; i < descriptors_object.rows; i++)
		if (matches[i].distance < 2 * min_dist)
			good_matches.push_back(matches[i]);

	Mat img_matches;
	try
	{
		drawMatches(referencia, keypoints_object, frame, keypoints_scene,
					good_matches, img_matches, Scalar::all(-1), Scalar::all(-1),
					std::vector<char>(), DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
	}
	catch (cv::Exception &e)
	{
		cerr << "deja de funcionar en draw Matches" << endl;
	}
	//-- Localize the object
	std::vector<Point2f> obj;
	std::vector<Point2f> scene;
	for (size_t i = 0; i < good_matches.size(); i++)
	{
		//-- Get the keypoints from the good matches
		obj.push_back(keypoints_object[good_matches[i].queryIdx].pt);
		scene.push_back(keypoints_scene[good_matches[i].trainIdx].pt);
	}
	Mat H;
	try
	{
		H = findHomography(obj, scene, RANSAC);
	}
	catch (cv::Exception &e)
	{
		cerr << "deja de funcionar en findHomography" << endl;
	}
	//-- Get the corners from the image_1 ( the object to be "detected" )
	std::vector<Point2f> obj_corners(4);
	obj_corners[0] = cvPoint(0, 0);
	obj_corners[1] = cvPoint(referencia.cols, 0);
	obj_corners[2] = cvPoint(referencia.cols, referencia.rows);
	obj_corners[3] = cvPoint(0, referencia.rows);
	std::vector<Point2f> scene_corners(4);
	try
	{
		perspectiveTransform(obj_corners, scene_corners, H);
	}
	catch (cv::Exception &e)
	{
		cerr << "deja de funcionar en perspective transform" << endl;
	}
	//-- Draw lines between the corners (the mapped object in the scene - image_2 )
	line(img_matches, scene_corners[0] + Point2f(referencia.cols, 0), scene_corners[1] + Point2f(referencia.cols, 0), Scalar(0, 0, 255), 4);
	line(img_matches, scene_corners[1] + Point2f(referencia.cols, 0), scene_corners[2] + Point2f(referencia.cols, 0), Scalar(0, 255, 0), 4);
	line(img_matches, scene_corners[2] + Point2f(referencia.cols, 0), scene_corners[3] + Point2f(referencia.cols, 0), Scalar(0, 255, 0), 4);
	line(img_matches, scene_corners[3] + Point2f(referencia.cols, 0), scene_corners[0] + Point2f(referencia.cols, 0), Scalar(255, 0, 0), 4);

	// Conseguir rectangulo y de ahi sacar info de area y AR
	cv::Point2f offset(frame.cols * 0.01, frame.rows * 0.01);
	try
	{
		Rect candidate(scene_corners[0], scene_corners[2] + offset);
		Mat roi = frame(candidate);
		if (roi.rows != 0)
		{
			float ar = roi.cols / roi.rows;
			float area = roi.cols * roi.rows;
			if (ar < 1.6 && area > 33500)
			{
				imshow("dni ROI", roi);
				dniROI = roi; //Imagen a devolver
				return true;
			}
		}
	}
	catch (cv::Exception &e)
	{
		cerr << "al final del SIFT" << endl;
	}
	return false;
}
