/*==============================================================================

  Program: 3D Slicer

  Portions (c) Copyright Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

// Slicer includes
#include <vtkSlicerColorLogic.h>
#include <vtkSlicerVolumesLogic.h>
#include <qSlicerCoreApplication.h>
#include <qSlicerModuleManager.h>
#include <qSlicerAbstractCoreModule.h>

// MultiVolumeExplorer includes
#include "vtkSlicerMultiVolumeExplorerLogic.h"

// MRML includes
#include <vtkMRMLScalarVolumeDisplayNode.h>
#include <vtkMRMLScalarVolumeNode.h>
#include <vtkMRMLMultiVolumeNode.h>
#include <vtkMRMLVolumeArchetypeStorageNode.h>

// VTK includes
#include <vtkDoubleArray.h>
#include <vtkStringArray.h>
#include <vtkNew.h>

// ITK includes
#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkImage.h>
#include <itkImageSeriesReader.h>

// STD includes
#include <cassert>

// DCMTK includes
#include <dcmtk/dcmdata/dcmetinf.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/dcmdata/dcdict.h>
#include <dcmtk/dcmdata/cmdlnarg.h>
#include <dcmtk/ofstd/ofconapp.h>
#include <dcmtk/ofstd/ofstd.h>
#include <dcmtk/ofstd/ofdatime.h>
#include <dcmtk/dcmdata/dcuid.h>         /* for dcmtk version name */
#include <dcmtk/dcmdata/dcdeftag.h>      /* for DCM_StudyInstanceUID */

// STD includes
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>


//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerMultiVolumeExplorerLogic);

//----------------------------------------------------------------------------
vtkSlicerMultiVolumeExplorerLogic::vtkSlicerMultiVolumeExplorerLogic()
{
}

//----------------------------------------------------------------------------
vtkSlicerMultiVolumeExplorerLogic::~vtkSlicerMultiVolumeExplorerLogic()
{
}

//----------------------------------------------------------------------------
void vtkSlicerMultiVolumeExplorerLogic::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//---------------------------------------------------------------------------
void vtkSlicerMultiVolumeExplorerLogic::InitializeEventListeners()
{
  vtkNew<vtkIntArray> events;
  events->InsertNextValue(vtkMRMLScene::NodeAddedEvent);
  events->InsertNextValue(vtkMRMLScene::NodeRemovedEvent);
  events->InsertNextValue(vtkMRMLScene::EndBatchProcessEvent);
  this->SetAndObserveMRMLSceneEventsInternal(this->GetMRMLScene(), events.GetPointer());
}

//---------------------------------------------------------------------------
void vtkSlicerMultiVolumeExplorerLogic::UpdateFromMRMLScene()
{
  assert(this->GetMRMLScene() != 0);
}

//---------------------------------------------------------------------------
void vtkSlicerMultiVolumeExplorerLogic
::OnMRMLSceneNodeAdded(vtkMRMLNode* vtkNotUsed(node))
{
}

//---------------------------------------------------------------------------
void vtkSlicerMultiVolumeExplorerLogic
::OnMRMLSceneNodeRemoved(vtkMRMLNode* vtkNotUsed(node))
{
}

//----------------------------------------------------------------------------
void vtkSlicerMultiVolumeExplorerLogic::RegisterNodes()
{
  if(!this->GetMRMLScene())
    {
    return;
    }
  this->GetMRMLScene()->RegisterNodeClass(vtkNew<vtkMRMLMultiVolumeNode>().GetPointer());
}

//----------------------------------------------------------------------------
// Parse the DICOM series and initialize the appropriate attributes, break
// into individual frames and return the number of frames found.
//
// Attributes to be populated:
//  DICOM.TE
//  DICOM.FA
//  DICOM.TR
//  MultiVolume.NumberOfFrames
//  MultiVolume.FrameIdentifyingUnit
//  MultiVolume.FrameLabels
//  MultiVolume.FrameFileList
//
int vtkSlicerMultiVolumeExplorerLogic
::InitializeMultivolumeNode(vtkStringArray *filenames, vtkMRMLMultiVolumeNode *mvNode)
{

  std::cout << "InitializeMultivolumeNode() called" << std::endl;

  // this function takes on input the location of a directory that stores a single
  //  DICOM series and a tag used to separate individual subvolumes from that series.
  // Returns the number of frames sved, and the array of extracted tag values
  // associated with each frame for the MV node.

  typedef itk::GDCMImageIO ImageIOType;
  typedef itk::GDCMSeriesFileNames InputNamesGeneratorType;
  typedef short PixelValueType;
  typedef itk::Image< PixelValueType, 3 > VolumeType;
  typedef itk::ImageSeriesReader< VolumeType > ReaderType;
  std::string result = "";
  std::vector<DcmDataset*> dcmDatasetVector;

  std::vector<DcmTagKey>  VolumeIdentifyingTags;

  DcmTagKey temporalPositionTag = DcmTagKey(0x0018,0x1060);
  DcmTagKey teTag = DcmTagKey(0x0018,0x0081);
  DcmTagKey faTag = DcmTagKey(0x0018,0x1314);
  DcmTagKey trTag = DcmTagKey(0x0018,0x0080);

  VolumeIdentifyingTags.push_back(DcmTagKey(temporalPositionTag)); // DCE
  VolumeIdentifyingTags.push_back(DcmTagKey(teTag)); // vTE
  VolumeIdentifyingTags.push_back(DcmTagKey(faTag)); // vFA
  VolumeIdentifyingTags.push_back(DcmTagKey(trTag)); // vTR

  std::vector<std::string>  VolumeIdentifyingTagNames;

  VolumeIdentifyingTagNames.push_back(std::string("TriggerTime"));
  VolumeIdentifyingTagNames.push_back(std::string("EchoTime")); 
  VolumeIdentifyingTagNames.push_back(std::string("FlipAngle")); 
  VolumeIdentifyingTagNames.push_back(std::string("RepetitionTime")); 

  /*
  std::vector<std::string> filenames;

  DIR *dp;
  struct dirent *dirp;
  if((dp  = opendir(dir.c_str())) == NULL) {
    cout << "Error(" << errno << ") opening " << dir << endl;
    return 0;
  }
  while ((dirp = readdir(dp)) != NULL) {
    if(dirp->d_name[0] == '.')
      continue;
    filenames.push_back(dir+std::string("/")+std::string(dirp->d_name));
  }
  closedir(dp);

  std::cout << "Processing directory " << dir << std::endl;
  */

  for(unsigned i=0;i<filenames->GetNumberOfValues();i++)
    {
    DcmFileFormat ff;
    OFCondition fs = ff.loadFile((const char*)filenames->GetValue(i));
    if(fs.good())
      {
      dcmDatasetVector.push_back(ff.getAndRemoveDataset());
      }
   else
      {
      std::cout << "Error loading file " << filenames->GetValue(i) << std::endl;
      return 0;
      }
    }

  std::cout << dcmDatasetVector.size() << " files loaded OK" << std::endl;
  std::map<int,std::vector<std::string> > tagVal2FileList;

  unsigned numberOfFrames = 0;
  for(unsigned i=0;i<VolumeIdentifyingTags.size();i++)
    {
    OFCondition status;
    DcmTagKey tag = VolumeIdentifyingTags[i];
    std::cerr << "Splitting by " << tag << std::endl;
    for(unsigned j = 0; j < filenames->GetNumberOfValues(); ++j)
      {
      DcmElement *el;
      char* str;
      status = dcmDatasetVector[j]->findAndGetElement(tag, el);
      if(status.bad())
        {
        std::cerr << "Failed to get element from " << filenames->GetValue(j) << std::endl;
        break;
        }
      status = el->getString(str);
      if(status.bad() || !str)
        {
        std::cerr << "Failed to process element for " << str << std::endl;
        break;
        }
      tagVal2FileList[atoi(str)].push_back(filenames->GetValue(j));
      }

    if(tagVal2FileList.size()<=1)
      // not a multivolume
      continue;

    std::cerr << std::endl << "Distinct values of tags found: ";

    std::ostringstream frameFileListStream;
    std::ostringstream frameLabelsStream;
    std::ostringstream numberOfFramesStream;

    for(std::map<int,std::vector<std::string> >::const_iterator it=tagVal2FileList.begin();
      it!=tagVal2FileList.end();++it)
      {
      char str[255];
      sprintf(str, "%i", it->first);
      std::cerr << it->first << " ";

      for(std::vector<std::string>::const_iterator fIt=(*it).second.begin();
        fIt!=(*it).second.end();++fIt)
        {
        frameFileListStream << *fIt;
        if(fIt != (*it).second.end())
          frameFileListStream << " ";
        }

      frameLabelsStream << it->first;

      if(numberOfFrames != tagVal2FileList.size())
        {
        frameLabelsStream << " ";
        }
      }

      numberOfFramesStream << tagVal2FileList.size();


      mvNode->SetAttribute("MultiVolume.FrameFileList", frameFileListStream.str().c_str());
      mvNode->SetAttribute("MultiVolume.FrameLabels", frameLabelsStream.str().c_str());
      mvNode->SetAttribute("MultiVolume.NumberOfFrames", numberOfFramesStream.str().c_str());
      mvNode->SetAttribute("MultiVolume.FrameIdentifyingDICOMTagName", VolumeIdentifyingTagNames[i].c_str());

      if(i==0)
        {
        // MV is a DCE sequence, store TE, TR and FA
        OFCondition status;
        DcmElement *el;
        char* str;
        status = dcmDatasetVector[0]->findAndGetElement(teTag, el);
        if(status.good())
          {
          el->getString(str);
          mvNode->SetAttribute("MultiVolume.DICOM.EchoTime", str);
          }
        status = dcmDatasetVector[0]->findAndGetElement(trTag, el);
        if(status.good())
          {
          el->getString(str);
          mvNode->SetAttribute("MultiVolume.DICOM.RepetitionTime", str);
          }
        status = dcmDatasetVector[0]->findAndGetElement(faTag, el);
        if(status.good())
          {
          el->getString(str);
          mvNode->SetAttribute("MultiVolume.DICOM.FlipAngle", str);
          }
        }

      std::cout << std::endl;

      break; // once MV is found, stop
    }
  return tagVal2FileList.size();
}


//----------------------------------------------------------------------------
int vtkSlicerMultiVolumeExplorerLogic
::ProcessDICOMSeries(std::string dir, std::string outputDir,
                     std::string dcmTag, vtkDoubleArray* tagValues)
{
  // this function takes on input the location of a directory that stores a single
  //  DICOM series and a tag used to separate individual subvolumes from that series.
  // It saves the individual subvolumes in the output directory ordered by alpha, and
  //  returns the number of subvolumes and the values of tags for each one.

  typedef itk::GDCMImageIO ImageIOType;
  typedef itk::GDCMSeriesFileNames InputNamesGeneratorType;
  typedef short PixelValueType;
  typedef itk::Image< PixelValueType, 3 > VolumeType;
  typedef itk::ImageSeriesReader< VolumeType > ReaderType;

  int i, j;

  std::cout << "Processing directory " << dir << std::endl;

  // each directory is handled dependent on the input series type
  ImageIOType::Pointer gdcmIO = ImageIOType::New();
  gdcmIO->LoadPrivateTagsOff();

  InputNamesGeneratorType::Pointer inputNames = InputNamesGeneratorType::New();
  inputNames->SetUseSeriesDetails(true);
  inputNames->SetDirectory(dir);

  itk::SerieUIDContainer seriesUIDs = inputNames->GetSeriesUIDs();
  int nSeriesUIDs = seriesUIDs.size();

  if(nSeriesUIDs != 1)
    {
    std::cerr << "Only one series is allowed!" << std::endl;
    return -1;
    }

  const ReaderType::FileNamesContainer & filenames =
      inputNames->GetFileNames(seriesUIDs[0]);
  ReaderType::Pointer reader = ReaderType::New();
  reader->SetImageIO( gdcmIO );
  reader->SetFileNames( filenames );

  try
    {
    std::cout << "Splitting series.... updating reader." << std::endl;
    reader->Update();
    }
  catch (itk::ExceptionObject &excp)
    {
    std::cout << "Error encountered: exiting." << std::endl;
    std::cerr << "Exception thrown while reading the series" << std::endl;
    std::cerr << excp << std::endl;
    return EXIT_FAILURE;
    }

  ReaderType::DictionaryArrayRawPointer inputDict =
      reader->GetMetaDataDictionaryArray();
  int nSlices = inputDict->size();

  nSlices = filenames.size();
  //std::string sortTag = "0018|1060"; // DCE GE: trigger time
  std::string sortTag = dcmTag;
  std::string tagVal;
  std::map<int,ReaderType::FileNamesContainer> tagVal2fileList;

  for(j = 0; j < nSlices; ++j)
    {
    //std::cout << "\n\n\n\n\n Processing slice " << j << std::endl;

    itk::ExposeMetaData<std::string>(*(*inputDict)[j], sortTag, tagVal);
    //std::cout << "Tag value found: " << tagVal << "(" << tagVal2fileList.size() << ")" << " ";
    tagVal2fileList[atoi(tagVal.c_str())].push_back(filenames[j]);
    }

  // map items should be sorted by key
  tagValues->SetNumberOfComponents(1);
  tagValues->SetNumberOfTuples(tagVal2fileList.size());
  tagValues->Allocate(tagVal2fileList.size());

  i = 0;

  for(std::map<int,ReaderType::FileNamesContainer>::const_iterator
    it=tagVal2fileList.begin(); it!=tagVal2fileList.end(); ++it,++i)
    {

    std::ostringstream tagValStr;
    double tagVal = (*it).first;
    tagValStr  << tagVal;

    char fname[255];
    sprintf(fname, "%s/%08i.nrrd", outputDir.c_str(), i);
    std::string seriesFileName(fname);

    StoreVolumeNode((*it).second, seriesFileName);

    tagValues->SetComponent(i, 0, tagVal);
    }

  return tagVal2fileList.size();
}

//----------------------------------------------------------------------------
void vtkSlicerMultiVolumeExplorerLogic
::StoreVolumeNode(const std::vector<std::string>& filenames,
                  const std::string& seriesFileName)
{
  vtkMRMLVolumeArchetypeStorageNode* sNode =
    vtkMRMLVolumeArchetypeStorageNode::New();
  vtkMRMLScalarVolumeNode *vNode =
    vtkMRMLScalarVolumeNode::New();
  sNode->SetFileName(filenames[0].c_str());
  sNode->ResetFileNameList();
  for(std::vector<std::string>::const_iterator
    it=filenames.begin();it!=filenames.end();++it)
    sNode->AddFileName(it->c_str());
  sNode->SetSingleFile(0);
  sNode->ReadData(vNode);

  sNode->SetFileName(seriesFileName.c_str());
  sNode->SetWriteFileFormat("nrrd");
  sNode->SetURI(NULL);
  sNode->WriteData(vNode);
  sNode->Delete();
  vNode->Delete();
}
