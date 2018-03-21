#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/Vector.h"
#include "cinder/gl/Fbo.h"
#include "cinder/gl/Texture.h"
#include "cinder/gl/GlslProg.h"
#include "cinder/ImageIo.h"
#include "cinder/Quaternion.h"
#include "cinder/Utilities.h"
#include "Resources.h"

// audio
#include "cinder/audio/Context.h"
#include "cinder/audio/MonitorNode.h"
#include "cinder/audio/Utilities.h"
#include "cinder/audio/Source.h"
#include "cinder/audio/Target.h"
#include "cinder/audio/dsp/Converter.h"
#include "cinder/audio/SamplePlayerNode.h"
#include "cinder/audio/SampleRecorderNode.h"
#include "cinder/audio/NodeEffects.h"
#include "cinder/audio/MonitorNode.h"
// UserInterface
#include "CinderImGui.h"
// Settings
#include "VDSettings.h"
// Session
#include "VDSession.h"
// Log
#include "VDLog.h"
// UI
#include "VDUI.h"
// Spout
#include "CiSpoutIn.h"
#include "CiSpoutOut.h"


using namespace ci;
using namespace ci::app;
using namespace std;
using namespace VideoDromm;

#define IM_ARRAYSIZE(_ARR)			((int)(sizeof(_ARR)/sizeof(*_ARR)))

class VideodrommVertexSphereApp : public App {

public:
	VideodrommVertexSphereApp();
	void mouseMove(MouseEvent event) override;
	void mouseDown(MouseEvent event) override;
	void mouseDrag(MouseEvent event) override;
	void mouseUp(MouseEvent event) override;
	void keyDown(KeyEvent event) override;
	void keyUp(KeyEvent event) override;
	void fileDrop(FileDropEvent event) override;
	void update() override;
	void draw() override;
	void cleanup() override;
	void setUIVisibility(bool visible);
private:
	// Settings
	VDSettingsRef					mVDSettings;
	// Session
	VDSessionRef					mVDSession;
	// Log
	VDLogRef							mVDLog;
	// UI
	VDUIRef								mVDUI;
	// handle resizing for imgui
	void									resizeWindow();
	bool									mIsResizing;
	// imgui
	float									color[4];
	float									backcolor[4];
	int										playheadPositions[12];
	int										speeds[12];

	float									f = 0.0f;
	char									buf[64];
	unsigned int					i, j;

	bool									mouseGlobal;

	string								mError;
	// fbo
	bool									mIsShutDown;
	Anim<float>						mRenderWindowTimer;
	void									positionRenderWindow();
	bool									mFadeInDelay;
	SpoutIn								mSpoutIn;
	SpoutOut 							mSpoutOut;
	ci::SurfaceRef 				mSurface;
	vec2 mMouse;
	float mAngle;
	float mRotationSpeed;
	vec3 mAxis;
	quat mQuat;
	gl::GlslProgRef mShader;
	gl::TextureRef		mTexture;
	gl::TextureRef		sTexture;
	//fbo
	gl::FboRef				mFbo;

	ivec2 mOutputResolution;

	// audio
	float							*mData;
	float							maxVolume;
	audio::InputDeviceNodeRef		mLineIn;
	audio::MonitorSpectralNodeRef	mMonitorLineInSpectralNode;
	vector<float>					mMagSpectrum;
	// number of frequency bands of our spectrum
	static const int				kBands = 1024;
	float							mAudioMultFactor;
	bool							mRotate;
};


VideodrommVertexSphereApp::VideodrommVertexSphereApp()
	: mSpoutOut("VertexSphere", app::getWindowSize())
{
	setWindowSize(640, 480);
	setFrameRate(60.0f);
	mRotationSpeed = 0.010f;
	mAngle = 0.0f;
	mAxis = vec3(0.0f, 1.0f, 0.0f);
	mQuat = quat(mAngle, mAxis);
	mRotate = true;

	mShader = gl::GlslProg::create(loadAsset("mShader.vert"), loadAsset("mShader.frag"));
	mTexture = gl::Texture::create(loadImage(loadAsset("0.jpg")));//ndf.jpg
																			 /*mTexture.setWrap(GL_REPEAT, GL_REPEAT);
																			 mTexture.setMinFilter(GL_NEAREST);
																			 mTexture.setMagFilter(GL_NEAREST);*/
	sTexture = gl::Texture::create(loadImage(loadAsset("0.jpg")));

	gl::enableDepthRead();

	mOutputResolution = ivec2(640, 480);

	mFbo = gl::Fbo::create(640, 480);
	//mFbo->getTexture(0).setFlipped(true);
	//audio
	// linein
	auto ctx = audio::Context::master();
	mLineIn = ctx->createInputDeviceNode();

	auto scopeLineInFmt = audio::MonitorSpectralNode::Format().fftSize(2048).windowSize(1024);
	mMonitorLineInSpectralNode = ctx->makeNode(new audio::MonitorSpectralNode(scopeLineInFmt));

	mLineIn >> mMonitorLineInSpectralNode;

	mLineIn->enable();
	ctx->enable();
	// audio in multiplication factor
	mAudioMultFactor = 1.0;
	maxVolume = 0.0f;
	mData = new float[1024];
	for (int i = 0; i < 1024; i++)
	{
		mData[i] = 0;
	}

}
void VideodrommVertexSphereApp::positionRenderWindow() {
	mVDSettings->mRenderPosXY = ivec2(mVDSettings->mRenderX, mVDSettings->mRenderY);//20141214 was 0
	setWindowPos(mVDSettings->mRenderX, mVDSettings->mRenderY);
	setWindowSize(mVDSettings->mRenderWidth, mVDSettings->mRenderHeight);
}
void VideodrommVertexSphereApp::setUIVisibility(bool visible)
{
	if (visible)
	{
		showCursor();
	}
	else
	{
		hideCursor();
	}
}
void VideodrommVertexSphereApp::fileDrop(FileDropEvent event)
{
	string ext = "";
	// use the last of the dropped files
	fs::path mPath = event.getFile(event.getNumFiles() - 1);
	string mFile = mPath.string();
	if (mFile.find_last_of(".") != std::string::npos) ext = mFile.substr(mFile.find_last_of(".") + 1);

	if (ext == "png" || ext == "jpg")
	{
		mTexture = gl::Texture::create(loadImage(mFile));

	}
}
void VideodrommVertexSphereApp::update()
{
	mVDSession->setFloatUniformValueByIndex(mVDSettings->IFPS, getAverageFps());
	mVDSession->update();


	//audio
	mMagSpectrum = mMonitorLineInSpectralNode->getMagSpectrum();
	if (mMagSpectrum.empty())
		return;
	if (mRotate)
	{
		maxVolume = 0.0;
		size_t mDataSize = mMagSpectrum.size();
		if (mDataSize > 0)
		{
			float mv;
			float db;
			for (size_t i = 0; i < mDataSize; i++)
			{
				float f = mMagSpectrum[i];
				db = audio::linearToDecibel(f);
				f = db * mAudioMultFactor;
				if (f > maxVolume)
				{
					maxVolume = f; mv = f;
				}
			}
		}
		mAngle += mRotationSpeed;
	}
	else
	{
		maxVolume = 0.0;
	}
	//mQuat.set(mAxis, mAngle);
	getWindow()->setTitle("(" + toString(floor(getAverageFps())) + " fps) Sphere");
}
void VideodrommVertexSphereApp::cleanup()
{
	if (!mIsShutDown)
	{
		mIsShutDown = true;
		CI_LOG_V("shutdown");
		ui::disconnectWindow(getWindow());
		
		// save settings
		mVDSettings->save();
		mVDSession->save();
		quit();
	}
}
void VideodrommVertexSphereApp::mouseMove(MouseEvent event)
{
	
}
void VideodrommVertexSphereApp::mouseDown(MouseEvent event)
{
	mRotate = !mRotate;
}
void VideodrommVertexSphereApp::mouseDrag(MouseEvent event)
{
		
}
void VideodrommVertexSphereApp::mouseUp(MouseEvent event)
{
	
}

void VideodrommVertexSphereApp::keyDown(KeyEvent event)
{
	if (!mVDSession->handleKeyDown(event)) {
		switch (event.getCode()) {
		case KeyEvent::KEY_ESCAPE:
			// quit the application
			quit();
			break;
		case KeyEvent::KEY_h:
			// mouse cursor and ui visibility
			mVDSettings->mCursorVisible = !mVDSettings->mCursorVisible;
			setUIVisibility(mVDSettings->mCursorVisible);
			break;
		}
	}
}
void VideodrommVertexSphereApp::keyUp(KeyEvent event)
{
	if (!mVDSession->handleKeyUp(event)) {
	}
}
void VideodrommVertexSphereApp::resizeWindow()
{
	mVDUI->resize();
	mVDSession->resize();
}

void VideodrommVertexSphereApp::draw()
{
	gl::clear(Color::black());
	unsigned int width, height;

	mFbo->bindFramebuffer();
	gl::clear();
	//gl::setViewport(getWindowBounds());

	//mTexture->enableAndBind();
	mShader->bind();
	/*if (maxVolume > 0)
	{*/
	mShader->uniform("normScale", maxVolume);
	//}
	//else
	//{
	//	mShader.uniform("normScale", (mMouse.x) / 5.0f);// (mMouse.x)
	//}
	mShader->uniform("colorMap", 0);
	mShader->uniform("displacementMap", 0);
	gl::pushModelView();
	gl::translate(vec3(0.5f * 640, 0.5f * 480, 0));
	gl::rotate(mQuat);
	gl::drawSphere(vec3(0, 0, 0), 140, 500);
	gl::popModelView();
	//mShader->unbind();
	mTexture->unbind();

	mFbo->unbindFramebuffer();
	sTexture = mFbo->getColorTexture();

	gl::clear();
	gl::draw(sTexture);
}

void prepareSettings(App::Settings *settings)
{
	settings->setWindowSize(640, 480);
}

CINDER_APP(VideodrommVertexSphereApp, RendererGl, prepareSettings)
