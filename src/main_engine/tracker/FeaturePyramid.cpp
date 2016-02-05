#include "main_engine/tracker/FeaturePyramid.h"

#include "main_engine/utils/settings.h"

FeaturePyramid::FeaturePyramid()
{}

FeaturePyramid::~FeaturePyramid()
{
  SafeDeleteArray(prevLevels);
  SafeDeleteArray(currLevels);
  SafeDeleteArray(currLevelsBuffer);
}

void FeaturePyramid::create(int nW, int nH, int nChannels, int numLevels)
{
  m_nWidth = nW;
  m_nHeight = nH;
  m_nNumChannels = nChannels;
  m_nNumLevels = numLevels;
  m_bInitialized = false;

  prevLevels = new FeatureLevel[numLevels];
  currLevels = new FeatureLevel[numLevels];
  currLevelsBuffer = new FeatureLevel[numLevels];
}

void FeaturePyramid::InitializeDB(const char* db_path)
{

  // check the status of the database

  std::cout << "Openning lmdb " << db_path;
  // check the folder already exists

  bool valid;

  valid = (mdb_env_create(&mdb_env) ==  MDB_SUCCESS) &&
    (mdb_env_set_mapsize(mdb_env, 10485760000000) == MDB_SUCCESS) &&
    (mdb_env_open(mdb_env, db_path, 0, 0664) == MDB_SUCCESS) &&
    (mdb_txn_begin(mdb_env, NULL, 0, &mdb_txn) == MDB_SUCCESS) &&
    (mdb_open(mdb_txn, NULL, 0, &mdb_dbi) == MDB_SUCCESS);

  // assert(mdb_cursor_open(mdb_txn, mdb_dbi, &mdb_cursor) == MDB_SUCCESS);
  // int mdb_status = mdb_cursor_get(mdb_cursor, &mdb_key, &mdb_data, MDB_NEXT);
  // if(mdb_status == MDB_NOTFOUND)
  //   valid = false;
  // else{
  //   MDB_CHECK(mdb_status);
  //   valid = true;
  // }

  if(!valid)
    {
      std::cout << "Open lmdb error" << std::endl;
      throw 20;
    };

}

void FeaturePyramid::setupCameraPyramid(int numLevels, CameraInfo& camInfo)
{
  double factor = featureSettings.scalingFactor;

  for(int i = 0; i < numLevels; ++i)
    {
      double factor;
      if(featureSettings.featurePyramidSamplingFactors.size() > 0)
        {
          factor = featureSettings.scalingFactor *
            featureSettings.featurePyramidSamplingFactors[i];
        }
      else
        {
          factor = featureSettings.scalingFactor;
        }

      CameraInfo camInfoLevel = camInfo;
      // update the parameters
      camInfoLevel.width = camInfo.width  / factor;
      camInfoLevel.height = camInfo.height  / factor;
      camInfoLevel.KK[0][0] = camInfo.KK[0][0] / factor;
      camInfoLevel.KK[0][2] = camInfo.KK[0][2] / factor;
      camInfoLevel.KK[1][1] = camInfo.KK[1][1] / factor;
      camInfoLevel.KK[1][2] = camInfo.KK[1][2] / factor;
      camInfoLevel.invKK[0][0] = camInfo.invKK[0][0] * factor;
      camInfoLevel.invKK[1][1] = camInfo.invKK[1][1] * factor;
      // the invKK[0][2] and invKK[1][2] remains the same
      camInfoLevels.push_back(camInfoLevel);
    }
}

void FeaturePyramid::setupPyramid(string key)
{
  // setup current pyramid
  mdb_key.mv_size = key.length();
  mdb_key.mv_data = reinterpret_cast<void*>(&key[0]);

  mdb_get(mdb_txn, mdb_dbi, &mdb_key, &mdb_data);

  // setup the 0th level of current feature image pyramid

  int data_size = mdb_data.mv_size;
  int channel_size = m_nHeight * m_nWidth * featureSettings.dataElemSize;
  int real_size =  m_nNumChannels * channel_size;
  int shift = (data_size - real_size) / featureSettings.dataElemSize;

  currLevelsBuffer[0].featureImageVec.resize(m_nNumChannels);
  currLevelsBuffer[0].featureImageGradXVec.resize(m_nNumChannels);
  currLevelsBuffer[0].featureImageGradYVec.resize(m_nNumChannels);

  int factor = 1;
  double blurSigma;

  for(int j = 0; j < m_nNumChannels; ++j)
    {
      for(int i = 0; i < m_nNumLevels; ++i)
        {
          if(i == 0)
            {
              switch(featureSettings.dataElemSize)
                {
                case 1:
                  {
                    unsigned char* data_pointer = reinterpret_cast<unsigned char*>(mdb_data.mv_data);
                    featureBufferImage = cv::Mat(m_nHeight,
                                                 m_nWidth,
                                                 featureSettings.dataTypeINT,
                                                 data_pointer + shift + j * channel_size);
                  }
                  break;
                case 4:
                  {
                    float* data_pointer = reinterpret_cast<float*>(mdb_data.mv_data);
                    featureBufferImage = cv::Mat(m_nHeight,
                                                 m_nWidth,
                                                 featureSettings.dataTypeINT,
                                                 data_pointer + shift + j * channel_size);
                  }
                  break;
                case 8:
                  {
                    double* data_pointer = reinterpret_cast<double*>(mdb_data.mv_data);
                    featureBufferImage = cv::Mat(m_nHeight,
                                                 m_nWidth,
                                                 featureSettings.dataTypeINT,
                                                 data_pointer + shift + j * channel_size);
                  }
                  break;
                }
            }

          int blurSize = featureSettings.blurFeatureFilterSizes[i];
          if(featureSettings.blurFeatureSigmaSizes.size() > 0)
            blurSigma = featureSettings.blurFeatureSigmaSizes[i];
          else
            blurSigma = 3;

          if(blurSize > 0){
            cv::GaussianBlur(featureBufferImage,
                             blurBufferImage,
                             cv::Size(blurSize, blurSize),
                             blurSigma);
          }else
            featureBufferImage.copyTo(blurBufferImage);

          // do some proper downsampling at this point
          if(featureSettings.featurePyramidSamplingFactors.size() > 0){
            factor = featureSettings.featurePyramidSamplingFactors[i];
            resize(blurBufferImage,
                   currLevelsBuffer[i].featureImageVec[j],
                   cv::Size(),
                   1.0/factor,
                   1.0/factor);
          }
          else{
            blurBufferImage.copyTo(currLevelsBuffer[i].featureImageVec[j]);
          }

          // setup gradient
          double gradScale = featureSettings.featureGradientScalingFactors[i];

          cv::Scharr(currLevelsBuffer[i].featureImageVec[i],
                     currLevelsBuffer[i].featureImageGradXVec[i],
                     featureSettings.gradTypeINT,
                     1,0,
                     gradScale);

          cv::Scharr(currLevelsBuffer[i].featureImageVec[i],
                     currLevelsBuffer[i].featureImageGradYVec[i],
                     featureSettings.gradTypeINT,
                     0,1,
                     gradScale);

        }
    }

  // setup feature pyramid for previous frame
  if(!m_bInitialized)
    {
      prevLevels[0].featureImageVec.resize(m_nNumChannels);
      prevLevels[0].featureImageGradXVec.resize(m_nNumChannels);
      prevLevels[0].featureImageGradYVec.resize(m_nNumChannels);

      // copy over the data from currLevelsBuffer
      for(int i = 0; i < m_nNumLevels; ++i){
        for(int j = 0; j < m_nNumChannels; ++j){
          currLevelsBuffer[i].featureImageVec[j].copyTo( prevLevels[i].featureImageVec[j] );
          currLevelsBuffer[i].featureImageGradXVec[j].copyTo( prevLevels[i].featureImageGradXVec[j] );
          currLevelsBuffer[i].featureImageGradYVec[j].copyTo( prevLevels[i].featureImageGradYVec[j] );
        }
      }

      m_bInitialized = true;
    }
}

void FeaturePyramid::updateData()
{
  FeatureLevel* tempPointer;
  tempPointer = currLevelsBuffer;
  currLevelsBuffer = currLevels;
  currLevels = tempPointer;
}

void FeaturePyramid::updatePrev()
{
  // make sure prevLevels has already been initialized

  // exchange prevLevels and currLevels
  FeatureLevel* tempPointer;
  tempPointer = prevLevels;
  prevLevels = currLevels;
  currLevels = tempPointer;

}

CameraInfo& FeaturePyramid::getCameraInfo(int nLevel)
{
  return camInfoLevels[nLevel];
}

FeatureLevel& FeaturePyramid::getPrevFeatureLevel(int nLevel)
{
  return prevLevels[nLevel];
}

FeatureLevel& FeaturePyramid::getCurrFeatureLevel(int nLevel)
{
  return currLevels[nLevel];
}