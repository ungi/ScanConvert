#include "PlusConfigure.h"
#include "TrackedFrame.h"
#include "vtkImageCast.h"
#include "vtkImageData.h"
#include "vtkMetaImageSequenceIO.h"
#include "vtkSmartPointer.h"
#include "vtkTrackedFrameList.h"
#include "vtkUsScanConvert.h"
#include "vtkUsScanConvertCurvilinear.h"
#include "vtkUsScanConvertLinear.h"

#include "vtksys/CommandLineArguments.hxx"


void fillLinesImage(vtkUsScanConvert* scanConverter, vtkImageData* inputImageData, vtkImageData* outputImageData)
{
  int* linesImageExtent = scanConverter->GetInputImageExtent();
  int lineLengthPx = linesImageExtent[1] - linesImageExtent[0] + 1;
  int numScanLines = linesImageExtent[3] - linesImageExtent[2] + 1;

  float inputPixelValue = 0.0;

  int* inputExtent = inputImageData->GetExtent();
  for (int scanLine = 0; scanLine < numScanLines; scanLine ++ )
  {
    double start[4] = {0};
    double end[4] = {0};
    scanConverter->GetScanLineEndPoints(scanLine, start, end);

    double directionVectorX = static_cast<double>(end[0]-start[0])/(lineLengthPx-1);
    double directionVectorY = static_cast<double>(end[1]-start[1])/(lineLengthPx-1);
    for (int pointIndex=0; pointIndex<lineLengthPx; ++pointIndex)
    {
      int pixelCoordX = start[0] + directionVectorX * pointIndex;
      int pixelCoordY = start[1] + directionVectorY * pointIndex;
      if ( pixelCoordX<inputExtent[0] ||  pixelCoordX>inputExtent[1] || pixelCoordY<inputExtent[2] ||  pixelCoordY>inputExtent[3] )
      {
        outputImageData->SetScalarComponentFromFloat(pointIndex, scanLine, 0, 0, 0);
        continue; // outside of the specified extent
      }
      inputPixelValue = inputImageData->GetScalarComponentAsFloat(pixelCoordX, pixelCoordY, 0, 0);
      outputImageData->SetScalarComponentFromFloat(pointIndex, scanLine, 0, 0, inputPixelValue);
    }
  }
}


int main(int argc, char **argv)
{

  const int numScanLines = 100;
  const int numSamplesPerScanline = 1500;

  bool printHelp = false;
  vtksys::CommandLineArguments args;
  
  std::string inputFileName;
  std::string outputFileName;
  std::string configFileName;
  int verboseLevel=vtkPlusLogger::LOG_LEVEL_UNDEFINED;

  args.Initialize(argc, argv);
  args.AddArgument("--help", vtksys::CommandLineArguments::NO_ARGUMENT, &printHelp, "Print this help");
  args.AddArgument("--input-seq-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputFileName, "The filename for the input ultrasound sequence to process.");
  args.AddArgument("--config-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &configFileName, "The filename for input config file.");
  args.AddArgument("--output-seq-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &outputFileName, "The filename to write the processed sequence to.");
  args.AddArgument("--verbose", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &verboseLevel, "Verbose level (1=error only, 2=warning, 3=info, 4=debug, 5=trace)");

  if (!args.Parse())
  {
    std::cerr << "Error parsing arguments" << std::endl;
    std::cout << "Help: " << args.GetHelp() << std::endl;
    exit(EXIT_FAILURE);
  }

  if (printHelp)
  {
    std::cout << args.GetHelp() << std::endl;
    return EXIT_SUCCESS;
  }

  vtkPlusLogger::Instance()->SetLogLevel(verboseLevel);

  if (inputFileName.empty())
  {
    std::cerr << "--input-seq-file not found!" << std::endl;
    return EXIT_FAILURE;
  }
  
  if (configFileName.empty())
  {
    std::cerr << "--config-file not found!" << std::endl;
    return EXIT_FAILURE;
  }
  
  if (outputFileName.empty())
  {
    std::cerr << "--output-seq-file not found!" << std::endl;
    return EXIT_FAILURE;
  }

  // Read config file.

  vtkSmartPointer<vtkXMLDataElement> configRootElement = vtkSmartPointer<vtkXMLDataElement>::New();
  if (PlusXmlUtils::ReadDeviceSetConfigurationFromFile(configRootElement, configFileName.c_str())==PLUS_FAIL)
  {  
    LOG_ERROR("Unable to read configuration from file " << configFileName.c_str()); 
    return EXIT_FAILURE;
  }

  vtkXMLDataElement* boneFilterElement = configRootElement->FindNestedElementWithName("UsBoneFilter");
  if (boneFilterElement == NULL)
  {
    LOG_ERROR("Cannot find UsBoneFilter element in XML tree!");
    return PLUS_FAIL;
  }

  vtkXMLDataElement* scanConversionElement = boneFilterElement->FindNestedElementWithName("ScanConversion");
  if (scanConversionElement==NULL)
  {
    LOG_ERROR("Cannot find ScanConversion element in XML tree!");
    return PLUS_FAIL;
  }

  const char* transducerGeometry = scanConversionElement->GetAttribute("TransducerGeometry");
  if (transducerGeometry == NULL)
  {
    LOG_ERROR("Scan converter TransducerGeometry is undefined!");
    return PLUS_FAIL;
  }

  // Create scan converter.

  vtkSmartPointer<vtkUsScanConvert> scanConverter;
  if (STRCASECMP(transducerGeometry, "CURVILINEAR")==0)
  {
    scanConverter = vtkSmartPointer<vtkUsScanConvert>::Take(vtkUsScanConvertCurvilinear::New());
  }
  else if (STRCASECMP(transducerGeometry, "LINEAR")==0)
  {
    scanConverter = vtkSmartPointer<vtkUsScanConvert>::Take(vtkUsScanConvertLinear::New());
  }
  else
  {
    LOG_ERROR("Invalid scan converter TransducerGeometry: " << transducerGeometry);
    return PLUS_FAIL;
  }
  scanConverter->ReadConfiguration(scanConversionElement);

  // Read input image.

  vtkSmartPointer<vtkTrackedFrameList> inputFrameList = vtkSmartPointer<vtkTrackedFrameList>::New();
  inputFrameList->ReadFromSequenceMetafile(inputFileName.c_str());
  int numberOfFrames = inputFrameList->GetNumberOfTrackedFrames();
  
  // Create lines image (this is the image which holds scan lines in rows).

  int linesImageExtent[6]={0, numSamplesPerScanline-1, 0, numScanLines-1, 0, 0};
  scanConverter->SetInputImageExtent(linesImageExtent);

  vtkSmartPointer<vtkImageData> linesImage = vtkSmartPointer<vtkImageData>::New();
  linesImage->SetExtent(linesImageExtent);
  linesImage->AllocateScalars(VTK_UNSIGNED_CHAR, 1);

  // Create frame lists for lines images and output images.

  vtkSmartPointer<vtkTrackedFrameList> linesFrameList = vtkSmartPointer<vtkTrackedFrameList>::New();
  vtkSmartPointer<vtkTrackedFrameList> outputFrameList = vtkSmartPointer<vtkTrackedFrameList>::New();

  // Iterate thought every frame.

  for (int frameIndex = 0; frameIndex < numberOfFrames; frameIndex ++ )
  {
    TrackedFrame* inputFrame = inputFrameList->GetTrackedFrame(frameIndex);
    
    linesFrameList->AddTrackedFrame(inputFrame);
    TrackedFrame* linesFrame = linesFrameList->GetTrackedFrame(linesFrameList->GetNumberOfTrackedFrames()-1);
    linesFrame->GetImageData()->DeepCopyFrom(linesImage);  // Would there be a more efficient way to create this tracked frame?

    // Extract scan lines from image.

    fillLinesImage(scanConverter, inputFrame->GetImageData()->GetImage(), linesFrame->GetImageData()->GetImage() );
    
    // There would be some processing on the lines image here...

    // Convert lines image back to original geometry.

    scanConverter->SetInputData( linesFrame->GetImageData()->GetImage() );
    scanConverter->Update();

    // Allocate and store output image.

    outputFrameList->AddTrackedFrame(inputFrame);
    TrackedFrame* outputFrame = outputFrameList->GetTrackedFrame(outputFrameList->GetNumberOfTrackedFrames()-1);
    outputFrame->GetImageData()->DeepCopyFrom(scanConverter->GetOutput());
  }
  
  std::cout << "Writing output to file. Setting log level to error only, regardless of user specified verbose level." << std::endl;
  vtkPlusLogger::Instance()->SetLogLevel(1);
  
  outputFrameList->SaveToSequenceMetafile(outputFileName.c_str());
  linesFrameList->SaveToSequenceMetafile("LinesFrameList.mha");
  
  return EXIT_SUCCESS;
}

