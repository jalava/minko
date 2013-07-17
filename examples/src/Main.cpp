#include <time.h>

#include "minko/Minko.hpp"
#include "minko/MinkoJPEG.hpp"
#include "minko/MinkoPNG.hpp"
#include "minko/MinkoParticles.hpp"

#include "GLFW/glfw3.h"

#define FRAMERATE 60

using namespace minko::component;
using namespace minko::math;

ParticleSystem::Ptr particleSystem;
Rendering::Ptr renderingComponent;

auto mesh = scene::Node::create("mesh");
auto group = scene::Node::create("group");
auto camera	= scene::Node::create("camera");

void
printFramerate(const unsigned int delay = 1)
{
	static auto start = clock();
	static auto numFrames = 0;

	auto time = clock();
	auto deltaT = (float)(clock() - start) / CLOCKS_PER_SEC;

	++numFrames;
	if (deltaT > delay)
	{
		std::cout << ((float)numFrames / deltaT) << " fps." << std::endl;
		start = time;
		numFrames = 0;
	}
}

/*void screenshotFunc(int)
{
	const int width = 800, height = 600;

	char* pixels = new char[3 * width * height];

	glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);

	int i, j;
	FILE *fp = fopen("screenshot.ppm", "wb");
	fprintf(fp, "P6\n%d %d\n255\n", width, height);

	for (j = 0; j < height; ++j)
	{
		for (i = 0; i < width; ++i)
		{
			static unsigned char color[3];
			color[0] = pixels[(width * j + i) * 3 + 0];
			color[1] = pixels[(width * j + i) * 3 + 1];
			color[2] = pixels[(width * j + i) * 3 + 2];
			(void) fwrite(color, 1, 3, fp);
		}
	}

	fclose(fp);

	delete[] pixels;
}*/

int main(int argc, char** argv)
{
    glfwInit();
    auto window = glfwCreateWindow(800, 600, "Minko Examples", NULL, NULL);
    glfwMakeContextCurrent(window);

	auto context = render::OpenGLES2Context::create();
	auto assets	= AssetsLibrary::create(context)
		->registerParser<file::JPEGParser>("jpg")
		->registerParser<file::PNGParser>("png")
		->geometry("cube", geometry::CubeGeometry::create(context))
		//->geometry("sphere", geometry::SphereGeometry::create(context, 40))
		->queue("collage.jpg")
        ->queue("box3.png")
		->queue("DirectionalLight.effect")
		//->queue("VertexNormal.effect")
		->queue("Texture.effect")
		->queue("Red.effect")
		->queue("Basic.effect")
		->queue("Particles.effect")
		->queue("WorldSpaceParticles.effect");
	
#ifdef DEBUG
	assets->defaultOptions()->includePaths().push_back("effect");
	assets->defaultOptions()->includePaths().push_back("texture");
#else
	assets->defaultOptions()->includePaths().push_back("../../effect");
	assets->defaultOptions()->includePaths().push_back("../../texture");
#endif

	auto _ = assets->complete()->connect([context](AssetsLibrary::Ptr assets)
	{
		auto root   = scene::Node::create("root");
		

		root->addChild(group)->addChild(camera);

        renderingComponent = Rendering::create(assets->context());
        renderingComponent->backgroundColor(0x7f7f7fFF);
		camera->addComponent(renderingComponent);
        camera->addComponent(Transform::create());
        camera->component<Transform>()->transform()
            ->lookAt(Vector3::zero(), Vector3::create(0.f, 0.f, 3.f));
        camera->addComponent(PerspectiveCamera::create(.785f, 800.f / 600.f, .1f, 1000.f));


		mesh->addComponent(Transform::create());
		mesh->component<Transform>()->transform()
			->appendRotationZ(15)
			->appendTranslation(0.f, 0.f, -30.f);
		
		group->addChild(mesh);

		particleSystem = ParticleSystem::create(
			context,
			assets,
			3000,
			particle::sampler::RandomValue<float>::create(0.2, 0.8),
			particle::shape::Cylinder::create(1., 5., 5.),
			particle::StartDirection::NONE,
			0);
		particleSystem->isInWorldSpace(true);

		particleSystem->add(particle::modifier::StartForce::create(
			particle::sampler::RandomValue<float>::create(-2., 2.),
			particle::sampler::RandomValue<float>::create(8., 10.),
			particle::sampler::RandomValue<float>::create(-2., 2.)
			));

		particleSystem->add(particle::modifier::StartSize::create(
			particle::sampler::RandomValue<float>::create(0.1, 1.)
			));

		mesh->addComponent(particleSystem);
		particleSystem->updateRate(60);
		particleSystem->play();
	});

	assets->load();

	/*
	auto fx = assets->effect("directional light");

	std::cout << "== vertex shader compilation logs ==" << std::endl;
	std::cout << context->getShaderCompilationLogs(fx->shaders()[0]->vertexShader()) << std::endl;
	std::cout << "== fragment shader compilation logs ==" << std::endl;
	std::cout << context->getShaderCompilationLogs(fx->shaders()[0]->fragmentShader()) << std::endl;
	std::cout << "== program info logs ==" << std::endl;
	std::cout << context->getProgramInfoLogs(fx->shaders()[0]->id()) << std::endl;
	*/

	//glutTimerFunc(1000 / FRAMERATE, timerFunc, 0);
	//glutTimerFunc(1000, screenshotFunc, 0);

	while(!glfwWindowShouldClose(window))
    {
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
            camera->component<Transform>()->transform()->appendTranslation(0.f, 0.f, -.1f);
        else if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
            camera->component<Transform>()->transform()->appendTranslation(0.f, 0.f, .1f);
		mesh->component<Transform>()->transform()->prependRotationX(.01f);

	    renderingComponent->render();

	    printFramerate();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
 
    glfwTerminate();

    exit(EXIT_SUCCESS);
}