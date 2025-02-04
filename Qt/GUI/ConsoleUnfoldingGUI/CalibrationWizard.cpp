#include "CalibrationWizard.h"
#include "ui_CalibrationWizard.h"

CalibrationWizard::CalibrationWizard(QWidget *parent) :
	QWizard(parent),
	ui(new Ui::CalibrationWizard)
{
	ui->setupUi(this);

	//set default values
	ui->spinPatternLineSamples->setValue(Config::numLineSamples);
	ui->spinPatternCircleSamples->setValue(Config::numCircleSamples);

	ui->spinPatternCols->setValue(Config::patternPointsPerCol);
	ui->spinPatternRows->setValue(Config::patternPointsPerRow);
	ui->spinPatternSquareSize->setValue(Config::patternSquareSize);

	ui->spinSlantRes->setValue(Config::resSlantHeight);

	ui->spinConer->setValue(Config::radiusInner);
	ui->spinConeR->setValue(Config::radiusOuter);
	ui->spinConeh->setValue(Config::height);
	ui->checkConeIsToTip->setChecked(Config::whichHeight == CENTER_TO_TIP);


	ui->buttonStartIntrinsic->setEnabled(false);
	ui->buttonFindBlobs->setEnabled(false);
	ui->buttonGetMappings->setEnabled(false);


	ui->labelIntrinsicStatus->setText("");
	ui->labelBlobStatus->setText("");


	/*cv::FileStorage fs("cameraSettings.txt", cv::FileStorage::READ);
	if(!fs.isOpened())
		exit(0);
	fs["cameraMatrix"] >> this->cameraMatrix;
	fs["distCoeffs"] >> this->distCoeffs;
	fs.release();*/

	this->cameraMatrix = cv::Mat::eye(3, 3, CV_32F);
	this->distCoeffs = cv::Mat::zeros(5, 1, CV_32F);

	std::ostringstream stream;
	stream << "Camera Matrix: \n" << cameraMatrix << "\n\n" << "Distortion coefficients: \n" << distCoeffs;
	ui->textCameraMatrix->setText(QtOpencvCore::str2qstr(stream.str()));


	scene1 = new QGraphicsScene();
	scene2 = new QGraphicsScene();

	//calibration is skippable
	ui->wizPage1->setComplete(true);

	//TODO: DELETE
	//ui->wizPage2->setComplete(true);
	//ui->buttonExport->setEnabled(true);


	connect(ui->viewBlob, SIGNAL(mousePressedInView(QPointF, int)), this, SLOT(clickedInBlobView(QPointF, int)));
}

CalibrationWizard::~CalibrationWizard()
{
	delete ui;
}

void CalibrationWizard::on_buttonLoadIntrinsic_clicked()
{
    fileNamesCamCalib = QFileDialog::getOpenFileNames(this, "Select calibration images", "D:/lars/Documents/gitProjects/BA-Haalck-2016/img/calibration/chessboard", "Images (*.png *.jpg *.jpeg *.PNG *.JPG, *.JPEG)", nullptr, QFileDialog::DontUseNativeDialog);

	if(fileNamesCamCalib.size() > 0)
	{
		ui->buttonStartIntrinsic->setEnabled(true);
		ui->labelIntrinsicStatus->setText("");
	}
	else
		ui->labelIntrinsicStatus->setText("No images selected...");
}

void CalibrationWizard::on_buttonLoadCone_clicked()
{
    fileNameConeCalib = QFileDialog::getOpenFileName(this, "Select calibration image", "D:/lars/Documents/gitProjects/BA-Haalck-2016/img/calibration/chessboard", "Images (*.png *.jpg *.jpeg *.PNG *.JPG, *.JPEG)", nullptr ,QFileDialog::DontUseNativeDialog);

	if(fileNameConeCalib != "")
	{
		ui->buttonFindBlobs->setEnabled(true);
		ui->labelBlobStatus->setText("");
	}
	else
		ui->labelBlobStatus->setText("No images selected...");

}

void CalibrationWizard::on_buttonStartIntrinsic_clicked()
{
	ui->progressIntrinsic->setValue(0);
	ui->progressIntrinsic->setMaximum(fileNamesCamCalib.size());

	std::vector<std::vector<cv::Point2f>> imagePoints;
	std::vector<std::vector<cv::Point3f>> objectPoints;

	std::vector<cv::Point3f> currentobjectPoints = Calibration::getObjectPoints();
	cv::Size size;

	for(int i = 0; i < fileNamesCamCalib.size(); i++)
	{
		std::string fileName = fileNamesCamCalib.at(i).toStdString();
		cv::Mat img = cv::imread(fileName, CV_LOAD_IMAGE_GRAYSCALE);
		if(i == 0)
			size = img.size();

		cv::Mat imgWithCorners;
		std::vector<cv::Point2f> currentImagePoints = Calibration::getCorners(img, imgWithCorners);

		if(currentImagePoints.size() != 0)
		{
			imagePoints.push_back(currentImagePoints);
			objectPoints.push_back(currentobjectPoints);
		}
		ui->progressIntrinsic->setValue(i + 1);
		this->refresh();
	}

	Calibration::getIntrinsicParamters(imagePoints, objectPoints, this->cameraMatrix, this->distCoeffs, size);

	std::ostringstream stream;
	stream << "Camera Matrix: \n" << cameraMatrix << "\n\n" << "Distortion coefficients: \n" << distCoeffs;
	ui->textCameraMatrix->setText(QtOpencvCore::str2qstr(stream.str()));
}

void CalibrationWizard::drawKeyPoints()
{
	cv::Mat imgKey;
	cv::cvtColor(grey, imgKey, CV_GRAY2BGR);

	for(const auto& pt : keyPoints)
		cv::circle(imgKey, pt, 4, cv::Scalar(0,255,0), 2);

	scene1->addPixmap(QtOpencvCore::img2qpix(imgKey));
	ui->viewBlob->setScene(scene1);
	ui->viewBlob->show();

    if(keyPoints.size() != static_cast<size_t>(Config::numCircleSamples * Config::numLineSamples))
	{
		ui->labelBlobStatus->setText(QtOpencvCore::str2qstr(std::string("") + "not the right amount blobs detected. needed: " + std::to_string(Config::numCircleSamples * Config::numLineSamples) +
															", found: " + std::to_string(keyPoints.size())));
		ui->buttonGetMappings->setEnabled(false);
		ui->wizPage2->setComplete(false);
	}
	else
	{
		ui->labelBlobStatus->setText("");
		ui->buttonGetMappings->setEnabled(true);
	}

	std::ostringstream stream;
	for(const auto& pt : keyPoints)
		stream << pt << "\n";

	ui->textFoundBlobs->setText(QtOpencvCore::str2qstr(stream.str()));
}

void CalibrationWizard::on_buttonFindBlobs_clicked()
{
	std::string fileName = fileNameConeCalib.toStdString();
	grey = cv::imread(fileName, CV_LOAD_IMAGE_GRAYSCALE);

	cv::initUndistortRectifyMap(this->cameraMatrix, this->distCoeffs, cv::Mat(), this->cameraMatrix, grey.size(), CV_32FC1, remapXCam, remapYCam);

	if(!remapXCam.empty()) //if calibration was skipped
	{
		cv::Mat greyWarped;
		cv::remap(grey, greyWarped, remapXCam, remapYCam, cv::INTER_LINEAR);
		greyWarped.copyTo(grey);
	}

	greyOriginal = grey.clone();
	Config::scaleFactor = 1000.0f / grey.cols;
	cv::resize(grey, grey, cv::Size(1000, Misc::round(grey.rows * Config::scaleFactor)));
	Config::usedResWidth = grey.cols;
	Config::usedResHeight = grey.rows;


	keyPoints = DotDetection::detectDots(grey);

	drawKeyPoints();
	ui->viewBlob->fitInView(scene1->sceneRect(), Qt::KeepAspectRatio);
}


void CalibrationWizard::clickedInBlobView(QPointF point, int button)
{
	if(point.x() > 0 && point.y() > 0 && point.x() < grey.cols && point.y() < grey.rows)
	{
		if(button == 0)
		{
			keyPoints.push_back(cv::Point2f(point.x(), point.y()));
		}
		else
		{
			auto minElementIt = std::min_element(keyPoints.begin(), keyPoints.end(), [point](const cv::Point2f p1, const cv::Point2f p2)
			{
				return cv::norm(p1 - cv::Point2f(point.x(), point.y())) < cv::norm(p2 - cv::Point2f(point.x(), point.y()));
			});

			keyPoints.erase(minElementIt);

		}

		drawKeyPoints();
	}

}

void CalibrationWizard::on_buttonZoomPBlob_clicked()
{
	ui->viewBlob->scale(1.15, 1.15);
}

void CalibrationWizard::on_buttonZoomMBlob_clicked()
{
	ui->viewBlob->scale(1/1.15, 1/1.15);
}


void CalibrationWizard::on_buttonGetMappings_clicked()
{
	cone = Cone();
	cv::Mat orientation, canny;
	EdgeDetection::canny(grey, canny, orientation, Config::cannyLow, Config::cannyHigh, Config::cannyKernel, Config::cannySigma);


	ui->labelBlobStatus2->setText("ellipse detection..."); this->refresh();
	std::vector<Ellipse> ellipses = Ellipse::detectEllipses(canny);
	std::vector<std::vector<cv::Point2f>> pointsPerEllipse = Ellipse::getEllipsePointMappings(ellipses, keyPoints);
	Misc::sort(pointsPerEllipse, ellipses);

	ui->labelBlobStatus2->setText("reestimate ellipses.."); this->refresh();
	ellipses = Ellipse::reestimateEllipses(pointsPerEllipse, ellipses);

	ui->labelBlobStatus2->setText("fitting lines.."); this->refresh();
	std::vector<Line> lines = Line::fitLines(pointsPerEllipse);
	std::vector<std::vector<cv::Point3f>> worldCoords = cone.calculateWorldCoordinatesForSamples();

	cone.setEllipses(ellipses);
	cone.setLines(lines);
	cone.setSampleCoordsImage(pointsPerEllipse);
	cone.setSampleCoordsWorld(worldCoords);


	std::vector<cv::Point2f> imagePoints;
	std::vector<cv::Point3f> objectPoints;
	std::ostringstream stream;
	for(size_t i = 0; i < worldCoords.size(); i++)
	{
		for(size_t j = 0; j < worldCoords[i].size(); j++)
		{
			stream << pointsPerEllipse[i][j] << " --> " << worldCoords[i][j] << "\n";
			imagePoints.push_back(pointsPerEllipse[i][j]);
			objectPoints.push_back(worldCoords[i][j]);
		}
	}

	ui->textFoundBlobs->setText(QtOpencvCore::str2qstr(stream.str()));

	ui->labelBlobStatus2->setText("pose estimation..");
	this->refresh();

	cv::Mat proj = Transformation::getProjectiveMatrix(cone);
	this->projectionMatrix = proj;

	/*std::vector<cv::Point2f> reproj = Transformation::getReprojectionError(cone, proj);
	std::string reprojx = ""; std::string reprojy = "";
	for(const auto& pt : reproj)
	{
		reprojx += std::to_string(pt.x) + " ";
		reprojy += std::to_string(pt.y) + " ";

	}
	reprojx += ""; reprojy += "";
	std::cout << reprojx << "\n" << reprojy << std::endl;*/

	/*double avg = std::accumulate(reproj.begin(), reproj.end(), 0.0, [](double a, const cv::Point2f pt) { return a + cv::norm(pt);});
	avg /= reproj.size();

    std::cout << avg << std::endl;*/


	/*cv::Mat vis;
	cv::cvtColor(grey, vis, CV_GRAY2BGR);
	std::string reprojx = "["; std::string reprojy = "[";
	reprojx += "]"; reprojy += "]";

	std::cout << reprojx << "\n" << reprojy << std::endl;*/
	//cv::imshow("vis", vis);





	ui->wizPage2->setComplete(true);
}


void CalibrationWizard::refresh()
{
	QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void CalibrationWizard::on_buttonUnfold_clicked()
{
	Config::resSlantHeight = ui->spinSlantRes->value();
	cv::Mat showWarped;
	if(ui->radioForward->isChecked())
	{
		this->isForward = true;
		Transformation::getForwardWarpMaps(cone, remapXWarp, remapYWarp);
		Transformation::inverseRemap(greyOriginal, showWarped, remapXWarp, remapYWarp);

	}
	else
	{
		this->isForward = false;
		Transformation::getReverseWarpMaps(cone, remapXWarp, remapYWarp, projectionMatrix);
		cv::remap(greyOriginal, showWarped, remapXWarp, remapYWarp, cv::INTER_CUBIC, cv::BORDER_CONSTANT, cv::Scalar(0,0,0));


	}

	cv::imshow("warped", showWarped);

	ui->buttonExport->setEnabled(true);
	ui->wizPage3->setComplete(true);

}


void CalibrationWizard::on_buttonExport_clicked()
{
	QFileDialog dialog(this);
	dialog.setFileMode(QFileDialog::AnyFile);
	dialog.setNameFilter(tr("compressed XML (*.xml.gz)"));
	dialog.setDefaultSuffix("xml.gz");
    dialog.setOptions(QFileDialog::DontUseNativeDialog);

	QStringList fileNames;
	if(dialog.exec())
		fileNames = dialog.selectedFiles();

	QString file;
	if(fileNames.size() > 0)
		file = fileNames.at(0);

	//open file for write
	cv::FileStorage fs(QtOpencvCore::qstr2str(file), cv::FileStorage::WRITE);


	fs << "cameraMatrix" << this->cameraMatrix;
	fs << "distCoeffs" << this->distCoeffs;

	//fs << "remapXCam" << this->remapXCam; //can be calculated
	//fs << "remapYCam" << this->remapYCam;
	fs << "isForward" << this->isForward;
	fs << "remapXWarp" << this->remapXWarp;
	fs << "remapYWarp" << this->remapYWarp;

	fs.release();
}
