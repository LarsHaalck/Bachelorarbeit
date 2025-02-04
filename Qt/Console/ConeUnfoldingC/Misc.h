#ifndef MISC_H
#define MISC_H

//OPENCV
//#include <opencv2/highgui/highgui.hpp>
//#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/core/core.hpp>

//STD
#include <algorithm>

//USER
#include "Ellipse.h"

class Ellipse; //forward declaration

class Misc
{
public:
	//real/math modulo
	static int mod(int a, int b) { return ((a % b) + b) % b; }
	static double mod(double a, double b) { return std::fmod(std::fmod(a, b) + b, b); }
	static double mod(int a, double b) { return mod(static_cast<double>(a), b); }
	static double mod(double a, int b) { return mod(a, static_cast<double>(b)); }

	static int round(double a) { return static_cast<int>(std::lround(a)); }
	static float clamp(float n, float lower, float upper) { return std::max(lower, std::min(n, upper)); }

	static double radToDeg(double radian) { return radian * 180 / CV_PI; }
	static double degToRad(double degrees) { return degrees * CV_PI / 180; }

	static double angleWithX(const cv::Point2d& pt1, const cv::Point2d& pt2);
	static void sort(std::vector<std::vector<cv::Point2f>>& pointsPerEllipse, const std::vector<Ellipse>& ellipses);

};

#endif //MISC_H
