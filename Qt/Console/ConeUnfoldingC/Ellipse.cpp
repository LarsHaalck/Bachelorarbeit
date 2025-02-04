#include "Ellipse.h"

static bool isDebug = false;


Ellipse::Ellipse()
{

}
Ellipse::Ellipse(double x0, double y0, double a, double b, double theta) : _x0(x0), _y0(y0), _a(a), _b(b), _theta(theta)
{

}

Ellipse::Ellipse(cv::Point2d center, cv::Size2d semiAxes, double theta) : _x0(center.x), _y0(center.y), _a(semiAxes.width), _b(semiAxes.height), _theta(theta)
{

}

//convert quadratic cone section form to (x0,y0,a,b,theta) by using principal axis theorem
Ellipse::Ellipse(cv::Mat params)
{

	float a, b, c, d, e, f;
	a = params.at<float>(0,0);
	b = params.at<float>(1,0);
	c = params.at<float>(2,0);
	d = params.at<float>(3,0);
	e = params.at<float>(4,0);
	f = params.at<float>(5,0);

	//a*x^2 + b*y^2 + c*x*y + d*x + e*y + f = 0
	// [x,y] * [a, c/2; c/2, b] * [x; y] + [d, e] * [x; y] + f = 0

	//perform principal component analysis
	//calc eigenvalues and eigenvectors from [a, c/2; c/2, b]
	cv::Mat mat = cv::Mat::zeros(2,2, CV_32F);
	mat.at<float>(0,0) = a;
	mat.at<float>(0,1) = c/2;
	mat.at<float>(1,0) = c/2;
	mat.at<float>(1,1) = b;

	cv::Mat eigenValues, eigenVectors;
	cv::eigen(mat, true, eigenValues, eigenVectors);

	//sort eigenvalue descending by abs value
	if(std::abs(eigenValues.at<float>(0,0)) > std::abs(eigenValues.at<float>(1,0)))
	{
		//swap eigenvalues rows and eigenvector rows
		cv::Mat newEigenValues;
		cv::Mat newEigenVectors;
		cv::flip(eigenValues, newEigenValues, 0);
		cv::flip(eigenVectors, newEigenVectors, 0);

		newEigenValues.copyTo(eigenValues);
		newEigenVectors.copyTo(eigenVectors);
	}

	//A = U^T*D*U, where U = eigenVectors, D = diag(eigenvalues)
	_theta = std::atan(eigenVectors.at<float>(0,1) / eigenVectors.at<float>(0,0));

	//transform to [0,2*pi) for easier math
	_theta = std::fmod((std::fmod(_theta, CV_PI) + CV_PI), CV_PI);


	//(d,e) * U^T = (d',e')
	float dRot = d*eigenVectors.at<float>(0,0) + e*eigenVectors.at<float>(0,1);
	float eRot = d*eigenVectors.at<float>(1,0) + e*eigenVectors.at<float>(1,1);
	float x0Rot = -0.5f*dRot/eigenValues.at<float>(0,0);
	float y0Rot = -0.5f*eRot/eigenValues.at<float>(1,0);

	_x0 = eigenVectors.at<float>(0,0) * x0Rot + eigenVectors.at<float>(1,0) * y0Rot;
	_y0 = eigenVectors.at<float>(0,1) * x0Rot + eigenVectors.at<float>(1,1) * y0Rot;

	float remainder = -f+((x0Rot*x0Rot)*eigenValues.at<float>(0,0))+((y0Rot*y0Rot)*eigenValues.at<float>(1,0));
	_a = std::sqrt(remainder/eigenValues.at<float>(0,0));
	_b = std::sqrt(remainder/eigenValues.at<float>(1,0));
}

//get ellipse from rotated rect
Ellipse::Ellipse(cv::RotatedRect rect)
{
	_x0 = rect.center.x;
	_y0 = rect.center.y;

	//width >= height or theta + pi/2;
	_theta = rect.angle*CV_PI/180;
	if(rect.size.width >= rect.size.height)
	{
		_a = rect.size.width / 2;
		_b = rect.size.height / 2;

	}
	else
	{
		_a = rect.size.height / 2;
		_b = rect.size.width / 2;
		_theta += CV_PI/2;
		if(_theta >= 2*CV_PI)
			_theta -= 2*CV_PI;
	}
}

//eval ellipse at angle phi (radians) by using parametric equation
cv::Point2d Ellipse::evalAtPhi(double phi) const
{
	double x = getX() + _a*std::cos(phi)*std::cos(_theta) - _b*std::sin(phi)*std::sin(_theta);
	double y = getY() + _a*std::cos(phi)*std::sin(_theta) + _b*std::sin(phi)*std::cos(_theta);

	return cv::Point2d(x,y);
}

Ellipse Ellipse::operator*(double scalar)
{
	Ellipse e(*this);
	e._x0 *= scalar;
	e._y0 *= scalar;
	e._a *= scalar;
	e._b *= scalar;
	e._theta = std::fmod(scalar*_theta, 2 * CV_PI);
	return e;
}
Ellipse operator*(double scalar, Ellipse ellipse)
{
	return ellipse*scalar;
}
Ellipse Ellipse::operator+(Ellipse other)
{
	Ellipse e(*this);
	e._x0 += other._x0;
	e._y0 += other._y0;
	e._a += other._a;
	e._b += other._b;
	e._theta = std::fmod(e._theta + other._theta, 2 * CV_PI);

	return e;
}

//detect all (Config::numCircleSamples) ellipses in image using RANSAC
std::vector<Ellipse> Ellipse::detectEllipses(const cv::Mat& edgeImage)
{
	std::vector<Line> lines = Line::getLines(edgeImage);
	cv::Point weightedCenter = Line::getWeightedCenter(lines);

	cv::Mat debug = edgeImage.clone();
	cv::cvtColor(debug, debug, CV_GRAY2BGR);
	cv::circle(debug, weightedCenter, 3, cv::Scalar(0, 255, 255), -1);

	std::vector<Ellipse> ellipses;
	cv::Mat currentEdge = edgeImage.clone();

	cv::Point currentCenter = weightedCenter;
	while(ellipses.size() < static_cast<size_t>(Config::numCircleSamples))
	{
		cv::Mat debug2 = cv::Mat::zeros(debug.size(), CV_8UC3);
		//determine edge pixels of next ellipse by casting 200 rays from currentCenter
		std::vector<cv::Point> collisonPoints = rayCast(currentEdge, currentCenter, 200, debug2);

		//Ellipse e = Ellipse(cv::fitEllipse(collisonPoints));
		Ellipse e = robustEllipseFit(collisonPoints, edgeImage.size(), 7.0f, 1.5f, 200); //100 = 33 , 200 = 40 percent outlier

		cv::ellipse(debug, e.getEllipseAsRotatedRect(), cv::Scalar(0,255,0), 2);

		cv::RotatedRect delRect = e.getEllipseAsRotatedRect();
		delRect.size += cv::Size2f(50,50);

		//remove found ellipse from image
		cv::ellipse(currentEdge, delRect, cv::Scalar(0), -1);

		ellipses.push_back(e);

	}

	if(isDebug)
	{
		cv::imshow("debug currentEdge", currentEdge);
		cv::imshow("debug detectEllipses", debug);
	}

	return ellipses;

}

//casts numSamples rays from current caster and returns collision points
std::vector<cv::Point> Ellipse::rayCast(const cv::Mat& edgeImage, cv::Point caster, int numSamples, cv::Mat& img)
{
	cv::Mat debug = cv::Mat::zeros(edgeImage.rows, edgeImage.cols, CV_8U);
	cv::circle(debug, caster, 5, cv::Scalar(255), -1);
	cv::circle(img, caster, 5, cv::Scalar(0, 255, 255), -1);

	std::vector<cv::Point> points;
	for(int i = 0; i < numSamples; i++)
	{
		double alpha = (1.0*i/numSamples)*2*CV_PI;
		cv::Point2d d = cv::Point2d(std::cos(alpha),std::sin(alpha));
		cv::Point p = caster;

		double t = 0;
		while(t <= std::max(Config::usedResHeight, Config::usedResWidth) && p.x < edgeImage.cols && p.y < edgeImage.rows && p.x >= 0 && p.y >= 0)
		{

			if(edgeImage.at<uchar>(p) != 0)
			{
				cv::circle(img, p, 1, cv::Scalar(255, 255, 255), -1);
				debug.at<uchar>(p) = 255;
				points.push_back(p);
				break; //break at first hit

			}
			t += 0.1;
			p = caster + cv::Point(Misc::round(t*d.x), Misc::round(t*d.y));
		}
	}

	if (isDebug)
		cv::imshow("debug raycast", debug);
	return points;

}




//robust ellipse fitting using passed points
//dist determines max distance for point to be declard inlier
//returns only ellipes with (semi-) major to (semi-) minor ratio of maxAtoBRatio
Ellipse Ellipse::robustEllipseFit(const std::vector<cv::Point>& points, cv::Size szImg, double dist, float maxAtoBRatio, size_t numSteps)
{
	//ax2 + by2 + cxy + dx + ey + f = 0
	std::vector<cv::Point> shuffledPoints = points;
	cv::Mat debug = cv::Mat::zeros(Config::usedResHeight, Config::usedResWidth, CV_8UC3);

	Ellipse bestFittingEllipse = Ellipse();
	size_t maxNumInliers = 0;
	size_t currentStep = 0;
	std::random_device random_dev;
	while(currentStep < numSteps)
	{
		debug = cv::Mat::zeros(Config::usedResHeight, Config::usedResWidth, CV_8UC3);

		//get 6 random points
		std::mt19937 generator(random_dev());
		std::shuffle(shuffledPoints.begin(), shuffledPoints.end(), generator);
		std::vector<cv::Point> ellipsePoints = std::vector<cv::Point>(shuffledPoints.begin(), shuffledPoints.begin() + 6);

		//solve linear equation system and implicit cast from quadritic form to form (x0,y0,a,b,theta);
		Ellipse currentEllipse = solveEllipseEquation(ellipsePoints);

		//check if currentEllipse is out of image, or maxAtoBRatio is exceeded
		bool passedCheck = checkBoundaries(currentEllipse, szImg, maxAtoBRatio);
		if(!passedCheck) continue;

		if(isDebug)
		{
			cv::ellipse(debug, currentEllipse.getEllipseAsRotatedRect(), cv::Scalar(0,255,255),1);
			for(const auto& pt : ellipsePoints)
				cv::circle(debug, pt, 3, cv::Scalar(255, 255, 0), -1);
			cv::circle(debug, currentEllipse.getCenter(), 2, cv::Scalar(0, 255, 255), -1);

		}


		//count inliers
		bool isBadEllipse = false;
		size_t numInliers = 0;
		for(const auto& pt : shuffledPoints)
		{
			//double distPt = approxDist(currentEllipse, pt);
			double distPt = realDistTrans(currentEllipse, pt);
			if (distPt <= dist)
			{
				numInliers++;
				cv::circle(debug, pt, 2, cv::Scalar(0, 255, 0), -1);
			}
			else
			{
				//check if ellipse doesnt even fit for used points
				if(std::find(ellipsePoints.begin(), ellipsePoints.end(), pt) != ellipsePoints.end())
				{
					isBadEllipse = true;
					break;
				}

				cv::circle(debug, pt, 2, cv::Scalar(0, 0, 255), -1);
			}

		}

		if(isBadEllipse)
			continue;

		if(numInliers >= maxNumInliers)
		{
			bestFittingEllipse = currentEllipse;
			maxNumInliers = numInliers;
		}

		currentStep++;

	}

	if (isDebug)
		cv::imshow("debug robust ellipse", debug);

	return bestFittingEllipse;

}

//solve ellipse equation for 6 points using SVD
Ellipse Ellipse::solveEllipseEquation(const std::vector<cv::Point> &points)
{
	cv::Mat ellipseMat = cv::Mat::zeros(points.size(), 6, CV_32F);

	//fill matrix
	for(size_t r = 0; r < points.size(); r++)
	{
		cv::Point currentPoint = points.at(r);
		ellipseMat.at<float>(r,0) = static_cast<float>(currentPoint.x * currentPoint.x);
		ellipseMat.at<float>(r,1) = static_cast<float>(currentPoint.y * currentPoint.y);
		ellipseMat.at<float>(r,2) = static_cast<float>(currentPoint.x * currentPoint.y);
		ellipseMat.at<float>(r,3) = static_cast<float>(currentPoint.x);
		ellipseMat.at<float>(r,4) = static_cast<float>(currentPoint.y);
		ellipseMat.at<float>(r,5) = 1.0f;
	}

	//solve with SVD minimizing ellipseMat * (a,b,c,d,e,f) = 0 with |u| = 1
	cv::Mat solution;
	cv::SVD::solveZ(ellipseMat, solution);

	return Ellipse(solution);
}


bool Ellipse::checkBoundaries(const Ellipse& e, const cv::Size& sz, float maxAToBRatio)
{
	cv::Rect bounding = e.getEllipseAsRotatedRect().boundingRect();
	cv::Rect window = cv::Rect(0, 0, sz.width - 1, sz.height - 1);

	if (((bounding & window) != bounding) || (e.getSemiMajor() / e.getSemiMinor()) >= maxAToBRatio)
		return false;
	return true;

}

//wrapper for realDistTrans(const Ellipse& ellipse, const cv::Point& pt, cv::Point& intersect) but discarding intersection point
double Ellipse::realDistTrans(const Ellipse& ellipse, const cv::Point& pt)
{
	cv::Point intersect;
	return realDistTrans(ellipse, pt, intersect);
}

//transforms pt in ellipse coordinate system and first quadrant and calculates shortes euclidian distance
//intersect is point on ellipse with shortes euclidian distance to pt
double Ellipse::realDistTrans(const Ellipse& ellipse, const cv::Point& pt, cv::Point& intersect)
{
	//transform ellipse to center, rotate point into first quadrant
	cv::Point2d transPt = transformToOrigin(ellipse, cv::Point2d(pt.x, pt.y));
	int quad = 0;

	if (transPt.x >= 0 && transPt.y >= 0) //first quadrant
	{
		quad = 0;
	}
	else if (transPt.x <= 0 && transPt.y >= 0) //second quadrant
	{
		quad = 1;
		transPt.x *= -1;
	}
	else if (transPt.x <= 0 && transPt.y <= 0) //third quadrant
	{
		quad = 2;
		transPt = -1 * transPt;
	}
	else //fourth quadrant
	{
		quad = 3;
		transPt.y *= -1;
	}

	cv::Point2d intersect2d;
	double dist = realDist(ellipse, transPt, intersect2d);

	switch (quad)
	{
	case 0:
		break;
	case 1:
		intersect2d.x *= -1;
		break;
	case 2:
		intersect2d = -1 * intersect2d;
		break;
	case 3:
		intersect2d.y *= -1;
	}

	intersect = invTransformToOrigin(ellipse, intersect2d);
	return dist;
}

//calculates shortest euclidian distance to pt (must be in first quadrant)
//see Eberly
double Ellipse::realDist(const Ellipse& ellipse, const cv::Point2d& pt, cv::Point2d& intersect)
{
	double a = ellipse.getSemiMajor();
	double b = ellipse.getSemiMinor();
	double distance;
	double x0, x1;
	if (y1 > 0)
	{
		if (y0 > 0)
		{
			double z0 = pt.x / a;
			double z1 = pt.y / b;
			double g = z0*z0 + z1*z1 - 1;
			if (g != 0)
			{
				double r0 = (a / b)*(a / b);
				double sbar = findRoot(r0, z0, z1, g);
				x0 = r0 * pt.x / (sbar + r0);
				x1 = pt.y / (sbar + 1);
				distance = std::sqrt((x0 - pt.x)*(x0 - pt.x) + (x1 - pt.y)*(x1 - pt.y));
			}
			else
			{
				x0 = pt.x;
				x1 = pt.y;
				distance = 0;
			}
		}
		else //y0 == 0
		{
			x0 = 0;
			x1 = b;
			distance = std::fabs(pt.y - b);
		}
	}
	else //y1 == 0
	{
		double numer0 = a*pt.x;
		double denom0 = a*a - b*b;
		if (numer0 < denom0)
		{
			double xde0 = numer0 / denom0;
			x0 = a * xde0;
			x1 = b * std::sqrt(1 - xde0*xde0);
			distance = std::sqrt((x0 - pt.x)*(x0 - pt.x) + x1*x1);
		}
		else
		{
			x0 = a;
			x1 = 0;
			distance = std::fabs(pt.x - a);
		}
	}

	intersect = cv::Point2d(x0, x1);
	return distance;
}

//find root using nested intervals
//maxIterations is determined by maximum accuracy of double
double Ellipse::findRoot(double r0, double z0, double z1, double g)
{
	int maxIterations = std::numeric_limits<double>::digits - std::numeric_limits<double>::min_exponent;
	double n0 = r0*z0;
	double s0 = z1 - 1;
	double s1 = (g < 0 ? 0 : std::sqrt(n0*n0 + z1*z1) - 1);
	double s = 0;
	for (int i = 0; i < maxIterations; i++)
	{
		s = (s0 + s1) / 2;
		if (s == s0 || s == s1) { break; }
		double ratio0 = n0 / (s + r0);
		double ratio1 = z1 / (s + 1);
		g = ratio0*ratio0 + ratio1*ratio1 - 1;
		if (g > 0) { s0 = s; }
		else if (g < 0) { s1 = s; }
		else { break; }
	}
	return s;
}

//transforms point to ellipse coordinate system
cv::Point2d Ellipse::transformToOrigin(const Ellipse& ellipse, const cv::Point2d& point)
{
	cv::Point2d transformed = point - ellipse.getCenter();
	transformed = rotatePoint(transformed, -ellipse.getTheta());
	return transformed;
}

//transforms point from ellipse coordinate system back to normal coordinate system
cv::Point2d Ellipse::invTransformToOrigin(const Ellipse& ellipse, const cv::Point2d& point)
{
	cv::Point2d transformed = rotatePoint(point, ellipse.getTheta());
	transformed = transformed + ellipse.getCenter();
	return transformed;
}

//rotate point counter-clock wise by angle (radians)
cv::Point2d Ellipse::rotatePoint(const cv::Point2d pt, double angle)
{
	double x = std::cos(angle) * pt.x - std::sin(angle) * pt.y;
	double y = std::sin(angle) * pt.x + std::cos(angle) * pt.y;
	return cv::Point2d(x, y);
}


cv::RotatedRect Ellipse::getEllipseAsRotatedRect() const
{
	return cv::RotatedRect(cv::Point2d(_x0, _y0), cv::Size2d(2*_a, 2*_b), static_cast<float>(_theta*180/CV_PI));
}

//maps points to ellipses by calculating shortes distances
//returns vector vector of points. first element of outer vector contains all points on first ellipse and so on
std::vector<std::vector<cv::Point2f>> Ellipse::getEllipsePointMappings(const std::vector<Ellipse>& ellipses, const std::vector<cv::Point2f>& points)
{
	std::vector<std::vector<cv::Point2f>> pointsPerEllipse (Config::numCircleSamples, std::vector<cv::Point2f>(0));

	for(const cv::Point2f pt : points)
	{
		auto distMin = [pt](const Ellipse& e1, const Ellipse& e2) { return Ellipse::realDistTrans(e1, pt) < Ellipse::realDistTrans(e2, pt); };

		auto minElemIt = std::min_element(ellipses.begin(), ellipses.end(), distMin);
		pointsPerEllipse[std::distance(ellipses.begin(), minElemIt)].push_back(pt);
	}


	return pointsPerEllipse;

}

//reestimates Ellipes by using ellipse point mappings and LSQ to obtain optimal solution for all samples
std::vector<Ellipse> Ellipse::reestimateEllipses(const std::vector<std::vector<cv::Point2f>>& pointsPerEllipse, const std::vector<Ellipse>& ellipses)
{
	cv::Mat debug = cv::Mat::zeros(Config::usedResHeight, Config::usedResWidth, CV_8UC3);
	
	std::vector<Ellipse> reestimated;
	reestimated.reserve(ellipses.size());

	if(isDebug)
	{
		for(const Ellipse& e : ellipses)
		{
			cv::ellipse(debug, e.getEllipseAsRotatedRect(), cv::Scalar(0, 0, 255), 2);
		}
	}

	for(size_t i = 0; i < ellipses.size(); i++)
	{
		cv::Mat pointMat = cv::Mat(pointsPerEllipse[i]);

		cv::RotatedRect fittedEllipse = cv::fitEllipse(pointMat);
		reestimated.push_back(Ellipse(fittedEllipse));

		if(isDebug)
		{
			cv::ellipse(debug, fittedEllipse, cv::Scalar(0, 255, 0), 2);
			for(const auto& pt : pointsPerEllipse[i])
			{
				cv::circle(debug, pt, 3, cv::Scalar(0, 255, 0), 2);
			}
		}
	}

	if(isDebug)
		cv::imshow("debug reestimate", debug);

	return reestimated;
}


//calculates ellipse line intersection by solving quadratic formula
//ONLY ONE POINT IS RETURNED (even with 2 intersection points)
cv::Point2d Ellipse::ellipseLineIntersection(const Ellipse& ellipse, const Line& line)
{
	cv::Point2d start = transformToOrigin(ellipse, line.getStart());
	cv::Point2d end = transformToOrigin(ellipse, line.getEnd());

	double a = ellipse.getSemiMajor(); double b = ellipse.getSemiMinor();

	//solve using quadratic formula
	//values are obtained by pluggin in 2d line formula into ellipse equation
	double A = (end.x - start.x) * (end.x - start.x) / (a * a) + (end.y - start.y) * (end.y - start.y) / (b * b);
	double B = 2 * (start.x * (end.x - start.x) / (a * a) + start.y * (end.y - start.y) / (b * b));
	double C = start.x * start.x / (a * a) + start.y * start.y / (b * b) - 1;

	double discr = B * B - 4 * A * C;

	std::vector<double> solutions;
	if(discr == 0)
	{
		solutions.push_back(-B / (2 * A));
	}
	else if(discr > 0)
	{
		solutions.push_back(((-B + std::sqrt(discr)) / (2 * A)));
		solutions.push_back(((-B - std::sqrt(discr)) / (2 * A)));
	}

	cv::Point2d pt;
	for(double t : solutions)
	{
		if(t >= 0 && t <= 1)
			pt = start + t*(end - start);
	}

	cv::Point2d intersect = invTransformToOrigin(ellipse, pt);
	return intersect;
}


//get angle (radians) from point on ellipse
double Ellipse::getAngleAt(const Ellipse& ellipse, const cv::Point2d &pt)
{
	cv::Point2d transformed = transformToOrigin(ellipse, pt);
	double angle = std::atan2((transformed.y) / ellipse.getSemiMinor(), (transformed.x) / ellipse.getSemiMajor());
	angle = Misc::mod(angle, 2 * CV_PI);
	return angle;
}

//trivial getters
cv::Point2d Ellipse::getCenter() const { return cv::Point2d(_x0, _y0); }
double Ellipse::getX() const { return _x0; }
double Ellipse::getY() const { return _y0; }
double Ellipse::getSemiMajor() const { return _a; }
double Ellipse::getSemiMinor() const { return _b; }
double Ellipse::getMajor() const { return 2*_a; }
double Ellipse::getMinor() const { return 2*_b; }
cv::Size2d Ellipse::getAxes() const { return cv::Size2d(_a,_b); }
cv::Size2d Ellipse::getSemiAxes() const { return cv::Size2d(2*_a,2*_b); }
double Ellipse::getTheta() const { return _theta; }


