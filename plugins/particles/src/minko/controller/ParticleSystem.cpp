/*
Copyright (c) 2013 Aerys

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "ParticleSystem.hpp"

#include "minko/AssetsLibrary.hpp"

#include "minko/component/Rendering.hpp"
#include "minko/component/Surface.hpp"
#include "minko/component/Transform.hpp"

#include "minko/scene/Node.hpp"
#include "minko/scene/NodeSet.hpp"

#include "minko/render/Blending.hpp"
#include "minko/render/CompareMode.hpp"
#include "minko/render/ParticleVertexBuffer.hpp"
#include "minko/render/ParticleIndexBuffer.hpp"

#include "minko/particle/ParticleData.hpp"
#include "minko/particle/StartDirection.hpp"

#include "minko/particle/modifier/IParticleModifier.hpp"
#include "minko/particle/modifier/IParticleInitializer.hpp"
#include "minko/particle/modifier/IParticleUpdater.hpp"

#include "minko/particle/shape/Sphere.hpp"

#include "minko/particle/sampler/Sampler.hpp"

#include "minko/particle/tools/VertexComponentFlags.hpp"

#include "minko/math/Matrix4x4.hpp"

using namespace minko::component;
using namespace minko::particle;

#define EPSILON 0.001

ParticleSystem::ParticleSystem(AbstractContextPtr	context,
							   AssetsLibraryPtr		assets,
							   float				rate,
							   FloatSamplerPtr		lifetime,
							   ShapePtr				shape,
							   StartDirection		startDirection,
							   FloatSamplerPtr		startVelocity)
	: _countLimit			(16384),
	  _maxCount				(0),
	  _liveCount			(0),
	  _previousLiveCount	(0),
	  _particles			(),
	  _isInWorldSpace		(false),
	  _isZSorted			(false),
	  _useOldPosition		(false),
	  _rate					(1 / rate),
	  _lifetime				(lifetime),
	  _shape				(shape),
	  _startDirection		(startDirection),
	  _startVelocity		(startVelocity),
	  _createTimer			(0),
	  _format				(VertexComponentFlags::DEFAULT),
	  _updateStep			(0),
	  _playing				(false),
	  _emitting				(true),
	  _time					(0)
{
	_geometry = geometry::ParticlesGeometry::create(context);
	_material = data::Provider::create();
	
	_effect = assets->effect("particles");
	_surface = Surface::create(_geometry,
							   _material,
							   _effect);

	if (_shape == 0)
		_shape = shape::Sphere::create(10);

	_comparisonObject.system = (this);

	updateMaxParticlesCount();
}

void
ParticleSystem::initialize()
{
	_targetAddedSlot = targetAdded()->connect(std::bind(
		&ParticleSystem::targetAddedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2
	));	

	_targetRemovedSlot = targetRemoved()->connect(std::bind(
		&ParticleSystem::targetRemovedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2
	));
}

void
ParticleSystem::targetAddedHandler(AbsCompPtr	ctrl,
								   NodePtr 		target)
{	
	target->addComponent(_surface);

	_addedSlot = target->added()->connect(std::bind(
		&ParticleSystem::addedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2,
		std::placeholders::_3
	));

	_removedSlot = target->removed()->connect(std::bind(
		&ParticleSystem::removedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2,
		std::placeholders::_3
	));

	addedHandler(target->root(), target, target->parent());
}

void
ParticleSystem::targetRemovedHandler(AbsCompPtr ctrl,
									 NodePtr	target)
{
	target->addComponent(_surface);
	_addedSlot = nullptr;
	_removedSlot = nullptr;

	removedHandler(target->root(), target, target->parent());
}

void
ParticleSystem::addedHandler(NodePtr node,
								  NodePtr target,
								  NodePtr parent)
{
	_rootDescendantAddedSlot = target->root()->added()->connect(std::bind(
		&ParticleSystem::rootDescendantAddedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2,
		std::placeholders::_3
	));

	_rootDescendantRemovedSlot = target->root()->removed()->connect(std::bind(
		&ParticleSystem::rootDescendantRemovedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2,
		std::placeholders::_3
	));

	_componentAddedSlot = target->root()->componentAdded()->connect(std::bind(
		&ParticleSystem::componentAddedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2,
		std::placeholders::_3
	));

	_componentRemovedSlot = target->root()->componentRemoved()->connect(std::bind(
		&ParticleSystem::componentRemovedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2,
		std::placeholders::_3
	));

	rootDescendantAddedHandler(target->root(), target, target->parent());
}

void
ParticleSystem::removedHandler(NodePtr node,
									NodePtr target,
									NodePtr parent)
{
	_rootDescendantAddedSlot = nullptr;
	_rootDescendantRemovedSlot = nullptr;
	_componentAddedSlot = nullptr;
	_componentRemovedSlot = nullptr;

	rootDescendantRemovedHandler(target->root(), target, target->parent());
}

void
ParticleSystem::rootDescendantAddedHandler(NodePtr node,
												NodePtr target,
												NodePtr parent)
{
	auto rendererNodes = scene::NodeSet::create(node)
		->descendants(true)
        ->where([](scene::Node::Ptr node)
        {
            return node->hasComponent<Rendering>();
        });

	for (auto rendererNode : rendererNodes->nodes())
		for (auto renderer: rendererNode->components<Rendering>())
			addRenderer(renderer);
}

void
ParticleSystem::rootDescendantRemovedHandler(NodePtr node,
												  NodePtr target,
												  NodePtr parent)
{
	auto rendererNodes = scene::NodeSet::create(node)
		->descendants(true)
        ->where([](scene::Node::Ptr node)
        {
            return node->hasComponent<Rendering>();
        });

	for (auto rendererNode : rendererNodes->nodes())
		for (auto renderer: rendererNode->components<Rendering>())
			removeRenderer(renderer);
}

void
ParticleSystem::componentAddedHandler(NodePtr				node,
											NodePtr				target,
											AbsCompPtr	ctrl)
{
	auto renderer = std::dynamic_pointer_cast<Rendering>(ctrl);
	
	if (renderer)
		addRenderer(renderer);
}

void
ParticleSystem::componentRemovedHandler(NodePtr					node,
											  NodePtr					target,
											  AbsCompPtr	ctrl)
{
	auto renderer = std::dynamic_pointer_cast<Rendering>(ctrl);

	if (renderer)
		removeRenderer(renderer);
}

void
ParticleSystem::addRenderer(RenderingPtr renderer)
{
	if (_playing)
		_previousClock = clock();

	if (_enterFrameSlots.find(renderer) == _enterFrameSlots.end())
	{
		_enterFrameSlots[renderer] = renderer->enterFrame()->connect(std::bind(
				&ParticleSystem::enterFrameHandler,
				shared_from_this(),
				std::placeholders::_1));
	}
}

void
ParticleSystem::removeRenderer(RenderingPtr renderer)
{
		removeRenderer(renderer);
}

void
ParticleSystem::enterFrameHandler(RenderingPtr renderer)
{	
	if(!_playing)
		return;

	if (_isInWorldSpace)
		_toWorld = targets()[0]->components<Transform>()[0];

	clock_t now	= clock();
	float deltaT = (float)(now - _previousClock) / CLOCKS_PER_SEC;
	_previousClock = now;

	if (_updateStep == 0)
	{
			updateSystem(deltaT, _emitting);
			updateVertexBuffer();
	}
	else
	{
		bool changed = false;

		_time += deltaT;

		while (_time > _updateStep)
		{
			updateSystem(_updateStep, _emitting);
			changed = true;
			_time -= _updateStep;
		}
		if (changed)
			updateVertexBuffer();
	}
}

void
ParticleSystem::add(ModifierPtr	modifier)
{
	addComponents(modifier->getNeededComponents());

	modifier->setProperties(_material);
	
	IInitializerPtr i = std::dynamic_pointer_cast<modifier::IParticleInitializer> (modifier);

	if (i != 0)
	{
		_initializers.push_back(i);
		return;	
	}

	IUpdaterPtr u = std::dynamic_pointer_cast<modifier::IParticleUpdater> (modifier);

	if (u != 0)
		_updaters.push_back(u);
}

void
ParticleSystem::remove(ModifierPtr	modifier)
{
	IInitializerPtr i = std::dynamic_pointer_cast<modifier::IParticleInitializer> (modifier);

	if (i != 0)
	{
		for (auto it = _initializers.begin(); it != _initializers.end(); ++it)
		{
			if (*it == i)
			{
				_initializers.erase(it);
				modifier->unsetProperties(_material);
				updateVertexFormat();

				return;
			}
		}

		return;
	}
	
	IUpdaterPtr u = std::dynamic_pointer_cast<modifier::IParticleUpdater> (modifier);

	if (u != 0)
	{
		for (auto it = _updaters.begin(); it != _updaters.end(); ++it)
		{
			if (*it == u)
			{
				_updaters.erase(it);
				modifier->unsetProperties(_material);
				updateVertexFormat();

				return;
			}
		}
	}
}

bool
ParticleSystem::has(ModifierPtr 	modifier)
{
	IInitializerPtr i = std::dynamic_pointer_cast<modifier::IParticleInitializer> (modifier);

	if (i != 0)
	{
		for (auto it = _initializers.begin();
			it != _initializers.end();
			++it)
		{
			if (*it == i)
			{
				return true;
			}
		}

		return false;
	}
	
	IUpdaterPtr u = std::dynamic_pointer_cast<modifier::IParticleUpdater> (modifier);

	if (u != 0)
	{
		for (auto it = _updaters.begin(); it != _updaters.end(); ++it)
		{
			if (*it == u)
			{
				return true;
			}
		}

		return false;
	}

	return false;
}

void
ParticleSystem::fastForward(float time, unsigned int updatesPerSecond)
{
	float updateStep = _updateStep;

	if (updatesPerSecond != 0)
		updateStep = 1. / updatesPerSecond;

	while(time > updateStep)
	{
		updateSystem(updateStep, _emitting);
		time -= updateStep;
	}
}

void
ParticleSystem::updateSystem(float	timeStep, bool emit)
{
	if (emit && _createTimer < _rate)
		_createTimer += timeStep;

	for (unsigned particleIndex = 0; particleIndex < _particles.size(); ++particleIndex)
	{
		ParticleData& particle = _particles[particleIndex];

		if (particle.alive)
		{
			particle.timeLived += timeStep;

			particle.oldx = particle.x;
			particle.oldy = particle.y;
			particle.oldz = particle.z;

			if (particle.timeLived >= particle.lifetime)
				killParticle(particleIndex);
		}
	}

	if (_format & VertexComponentFlags::OLD_POSITION)
	{
		for (unsigned particleIndex = 0; particleIndex < _particles.size(); ++particleIndex)
		{
			ParticleData& particle = _particles[particleIndex];

			particle.oldx = particle.x;
			particle.oldy = particle.y;
			particle.oldz = particle.z;
		}
	}
	
	for (unsigned int i = 0; i < _updaters.size(); ++i)
		_updaters[i]->update(_particles, timeStep);

	for (unsigned particleIndex = 0; particleIndex < _particles.size(); ++particleIndex)
	{
		ParticleData& particle = _particles[particleIndex];
		
		if (!particle.alive && emit && _createTimer >= _rate)
		{
			_createTimer -= _rate;
			createParticle(particleIndex, *_shape, _createTimer);
			particle.lifetime = _lifetime->value();
		}
		
		particle.rotation += particle.startAngularVelocity * timeStep;

		particle.startvx += particle.startfx * timeStep;
		particle.startvy += particle.startfy * timeStep;
		particle.startvz += particle.startfz * timeStep;

		particle.x += particle.startvx * timeStep;
		particle.y += particle.startvy * timeStep;
		particle.z += particle.startvz * timeStep;
	}
}

void
ParticleSystem::createParticle(unsigned int 				particleIndex,
							   const shape::EmitterShape&	shape,
							   float						timeLived)
{
	ParticleData& particle = _particles[particleIndex];

	if (_startDirection == StartDirection::NONE)
	{
		shape.initPosition(particle);

		particle.startvx 	= 0;
		particle.startvy 	= 0;
		particle.startvz 	= 0;
	}
	else if (_startDirection == StartDirection::SHAPE)
	{
		shape.initPositionAndDirection(particle);
	}
	else if (_startDirection == StartDirection::RANDOM)
	{
		shape.initPosition(particle);
	}
	else if (_startDirection == StartDirection::UP)
	{
		shape.initPosition(particle);


		particle.startvx 	= 0;
		particle.startvy 	= 1;
		particle.startvz 	= 0;
	}
	else if (_startDirection == StartDirection::OUTWARD)
	{
		shape.initPosition(particle);


		particle.startvx 	= particle.x;
		particle.startvy 	= particle.y;
		particle.startvz 	= particle.z;
	}

	particle.oldx 	= particle.x;
	particle.oldy 	= particle.y;
	particle.oldz 	= particle.z;

	if (_isInWorldSpace)
	{
		const std::vector<float>& transform = _toWorld->transform()->data();

		float x = particle.x;
		float y = particle.y;
		float z = particle.z;

		particle.x = transform[0] * x + transform[1] * y + transform[2] * z + transform[3];
		particle.y = transform[4] * x + transform[5] * y + transform[6] * z + transform[7];
		particle.z = transform[8] * x + transform[9] * y + transform[10] * z + transform[11];

		if (_startDirection != StartDirection::NONE)
		{
			float vx = particle.startvx;
			float vy = particle.startvy;
			float vz = particle.startvz;

			particle.startvx = transform[0] * vx + transform[1] * vy + transform[2] * vz;
			particle.startvy = transform[4] * vx + transform[5] * vy + transform[6] * vz;
			particle.startvz = transform[8] * vx + transform[9] * vy + transform[10] * vz;
		}
	}

	if (_startDirection != StartDirection::NONE)
	{
		float norm = sqrt(particle.startvx * particle.startvx + 
						  particle.startvy * particle.startvy + 
						  particle.startvz * particle.startvz);

		float v = _startVelocity ? _startVelocity->value() : 1;

		particle.startvx 	= particle.startvx / norm * v;
		particle.startvy 	= particle.startvy / norm * v;
		particle.startvz 	= particle.startvz / norm * v;
	}

	particle.rotation 				= 0;
	particle.startAngularVelocity 	= 0;

	particle.timeLived	= timeLived;
			
	particle.alive 		= true;
	
	++_liveCount;

	for (unsigned int i = 0; i < _initializers.size(); ++i)
		_initializers[i]->initialize(particle, timeLived);
}

void
ParticleSystem::killParticle(unsigned int	particleIndex)
{
	_particles[particleIndex].alive = false;
	
	--_liveCount;
}

void
ParticleSystem::updateMaxParticlesCount()
{
	unsigned int value = ceilf(_lifetime->max() / _rate - EPSILON);
	value = value > _countLimit ? _countLimit : value;
	
	if (_maxCount == value)
		return;

	_maxCount = value;

	_liveCount = 0;
	for (unsigned int i = 0; i < _particles.size(); ++i)
	{
		if (_particles[i].alive)
		{
			if (_liveCount == _maxCount
				|| _particles[i].timeLived >= _lifetime->max())
				_particles[i].alive = false;
			else
			{
				++_liveCount;
				if (_particles[i].lifetime > _lifetime->max() || _particles[i].lifetime < _lifetime->min())
					_particles[i].lifetime = _lifetime->value();
			}
		}
	}
	resizeParticlesVector();
	_geometry->initStreams(_maxCount);
}

void
ParticleSystem::resizeParticlesVector()
{
	_particles.resize(_maxCount);
	if (_isZSorted)
	{
		_particleDistanceToCamera.resize(_maxCount);
		_particleOrder.resize(_maxCount);
		for (unsigned int i = 0; i < _particleOrder.size(); ++i)
			_particleOrder[i] = i;
	}
	else
	{
		_particleDistanceToCamera.resize(0);
		_particleOrder.resize(0);
	}
}

void
ParticleSystem::updateParticleDistancesToCamera()
{
	for (unsigned int i = 0; i < _particleDistanceToCamera.size(); ++i)
	{
		const ParticleData& particle = _particles[i];

		float x = particle.x;
		float y = particle.y;
		float z = particle.z;
		
		if (!_isInWorldSpace)
		{
			x = _localToWorld[0] * x + _localToWorld[4] * y + _localToWorld[8] * z + _localToWorld[12];
			y = _localToWorld[1] * x + _localToWorld[5] * y + _localToWorld[9] * z + _localToWorld[13];
			z = _localToWorld[2] * x + _localToWorld[6] * y + _localToWorld[10] * z + _localToWorld[14];
		}

		float deltaX = _cameraCoords[0] - x;
		float deltaY = _cameraCoords[1] - y;
		float deltaZ = _cameraCoords[2] - z;

		_particleDistanceToCamera[i] = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
	}
}

void
ParticleSystem::reset()
{
	if(_liveCount == 0)
		return;

	_liveCount = 0;

	for (unsigned particleIndex = 0; particleIndex < _particles.size(); ++particleIndex)
	{
		_particles[particleIndex].alive = false;
	}
}


void
ParticleSystem::addComponents(unsigned int components, bool blockVSInit)
{
	if (_format & components)
		return;

	_format |= components;

	render::ParticleVertexBuffer::Ptr vs = _geometry->vertices();
	unsigned int vertexSize = 5;

	vs->resetAttributes();

	if (_format & VertexComponentFlags::SIZE)
	{
		vs->addAttribute ("size", 1, vertexSize);
		vertexSize += 1;
	}

	if (_format & VertexComponentFlags::COLOR)
	{
		vs->addAttribute ("color", 3, vertexSize);
		vertexSize += 3;
	}

	if (_format & VertexComponentFlags::TIME)
	{
		vs->addAttribute ("time", 1, vertexSize);
		vertexSize += 1;
	}

	if (_format & VertexComponentFlags::OLD_POSITION)
	{
		vs->addAttribute ("old_Position", 3, vertexSize);
		vertexSize += 3;
	}

	if (_format & VertexComponentFlags::ROTATION)
	{
		vs->addAttribute ("rotation", 1, vertexSize);
		vertexSize += 1;
	}

	if (_format & VertexComponentFlags::SPRITEINDEX)
	{
		vs->addAttribute ("spriteIndex", 1, vertexSize);
		vertexSize += 1;
	}

	if (!blockVSInit)
		_geometry->initStreams(_maxCount);
}

unsigned int
ParticleSystem::updateVertexFormat()
{
	_format = VertexComponentFlags::DEFAULT;
	
	_geometry->vertices()->addAttribute("offset", 2, 0);
	_geometry->vertices()->addAttribute("position", 3, 2);

	for (auto it = _initializers.begin();
		 it != _initializers.end();
		 ++it)
	{
		addComponents((*it)->getNeededComponents(), true);
	}

	for (auto it = _updaters.begin();
		 it != _updaters.end();
		 ++it)
	{
		addComponents((*it)->getNeededComponents(), true);
	}

	if (_useOldPosition)
		addComponents(VertexComponentFlags::OLD_POSITION, true);
	
	_geometry->initStreams(_maxCount);
	return _format;
}

void
ParticleSystem::updateVertexBuffer()
{
	if (_liveCount == 0)
		return;

	if (_isZSorted)
	{
		updateParticleDistancesToCamera();
		std::sort(_particleOrder.begin(), _particleOrder.end(), _comparisonObject);
	}
	
	std::vector<float>& vsData = _geometry->vertices()->data();
	float* vertexIterator	= &(*vsData.begin());

	for (unsigned int particleIndex = 0; particleIndex < _maxCount; ++particleIndex)
	{
		ParticleData* particle;

		if (_isZSorted)
			particle = &_particles[_particleOrder[particleIndex]];
		else
			particle = &_particles[particleIndex];

		unsigned int i = 5;

		if (particle->alive)
		{
			setInVertexBuffer(vertexIterator, 2, particle->x);
			setInVertexBuffer(vertexIterator, 3, particle->y);
			setInVertexBuffer(vertexIterator, 4, particle->z);

			if (_format & VertexComponentFlags::SIZE)
				setInVertexBuffer(vertexIterator, i++, particle->size);

			if (_format & VertexComponentFlags::COLOR)
			{
				setInVertexBuffer(vertexIterator, i++, particle->r);
				setInVertexBuffer(vertexIterator, i++, particle->g);
				setInVertexBuffer(vertexIterator, i++, particle->b);
			}

			if (_format & VertexComponentFlags::TIME)
				setInVertexBuffer(vertexIterator, i++, particle->timeLived / particle->lifetime);

			if (_format & VertexComponentFlags::OLD_POSITION)
			{
				setInVertexBuffer(vertexIterator, i++, particle->oldx);
				setInVertexBuffer(vertexIterator, i++, particle->oldy);
				setInVertexBuffer(vertexIterator, i++, particle->oldz);
			}

			if (_format & VertexComponentFlags::ROTATION)
				setInVertexBuffer(vertexIterator, i++, particle->rotation);

			if (_format & VertexComponentFlags::SPRITEINDEX)
				setInVertexBuffer(vertexIterator, i++, particle->spriteIndex);

			vertexIterator += 4 * _geometry->vertexSize();
		}
	}
	std::static_pointer_cast<render::ParticleVertexBuffer>(_geometry->vertices())->update(_liveCount, _geometry->vertexSize());

	if (_liveCount != _previousLiveCount)
	{
		std::static_pointer_cast<render::ParticleIndexBuffer>(_geometry->indices())->update(_liveCount);
		_previousLiveCount = _liveCount;
	}
}