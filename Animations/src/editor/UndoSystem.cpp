#include "editor/UndoSystem.h"
#include "animation/AnimationManager.h"
#include "animation/Animation.h"

namespace MathAnim
{
	class Command
	{
	public:
		Command() {}
		virtual ~Command() {};

		virtual void execute(AnimationManagerData* const am) = 0;
		virtual void undo(AnimationManagerData* const am) = 0;
	};

	struct UndoSystemData
	{
		AnimationManagerData* am;
		Command** history;
		// The total number of commands that we can redo up to
		uint32 numCommands;
		// The offset where the current undo ptr is set
		// This is a ring buffer, so this should be <= to
		// (undoCursorHead + numCommands) % maxHistorySize
		uint32 undoCursorTail;
		// The offset for undo history beginning.
		uint32 undoCursorHead;
		uint32 maxHistorySize;
	};

	// -------------------------------------
	// Command Forward Decls
	// -------------------------------------
	class ModifyU8Vec4Command : public Command
	{
	public:
		ModifyU8Vec4Command(AnimObjId objId, const glm::u8vec4& oldVec, const glm::u8vec4& newVec, U8Vec4PropType propType)
			: objId(objId), oldVec(oldVec), newVec(newVec), propType(propType)
		{
		}

		virtual void execute(AnimationManagerData* const am) override;
		virtual void undo(AnimationManagerData* const am) override;

	private:
		AnimObjId objId;
		glm::u8vec4 oldVec;
		glm::u8vec4 newVec;
		U8Vec4PropType propType;
	};

	class ApplyU8Vec4ToChildrenCommand : public Command
	{
	public:
		ApplyU8Vec4ToChildrenCommand(AnimObjId objId, U8Vec4PropType propType)
			: objId(objId), oldProps({}), propType(propType)
		{
		}

		virtual void execute(AnimationManagerData* const am) override;
		virtual void undo(AnimationManagerData* const am) override;

	private:
		AnimObjId objId;
		U8Vec4PropType propType;
		std::unordered_map<AnimObjId, glm::u8vec4> oldProps;
	};

	namespace UndoSystem
	{
		// ----------------------------------------
		// Internal Implementations
		// ----------------------------------------
		static void pushAndExecuteCommand(UndoSystemData* us, Command* command);

		UndoSystemData* init(AnimationManagerData* const am, int maxHistory)
		{
			g_logger_assert(maxHistory > 1, "Cannot have a history of size '{}'. Must be greater than 1.", maxHistory);

			UndoSystemData* data = (UndoSystemData*)g_memory_allocate(sizeof(UndoSystemData));
			// TODO: Is there a way around this ugly hack while initializing? (Without classes...)
			data->am = am;
			data->numCommands = 0;
			data->maxHistorySize = maxHistory;
			data->history = (Command**)g_memory_allocate(sizeof(Command*) * maxHistory);
			data->undoCursorHead = 0;
			data->undoCursorTail = 0;

			return data;
		}

		void free(UndoSystemData* us)
		{
			for (uint32 i = us->undoCursorHead;
				i != ((us->undoCursorHead + us->numCommands) % us->maxHistorySize);
				i = ((i + 1) % us->maxHistorySize))
			{
				// Don't free the tail, nothing ever gets placed there
				if (i == ((us->undoCursorHead + us->numCommands) % us->maxHistorySize)) break;

				us->history[i]->~Command();
				g_memory_free(us->history[i]);
			}

			g_memory_free(us->history);
			g_memory_zeroMem(us, sizeof(UndoSystemData));
			g_memory_free(us);
		}

		void undo(UndoSystemData* us)
		{
			// No commands to undo
			if (us->undoCursorHead == us->undoCursorTail)
			{
				return;
			}

			uint32 offsetToUndo = us->undoCursorTail - 1;
			// Wraparound behavior
			if (us->undoCursorTail == 0)
			{
				offsetToUndo = us->maxHistorySize - 1;
			}

			us->history[offsetToUndo]->undo(us->am);
			us->undoCursorTail = offsetToUndo;
		}

		void redo(UndoSystemData* us)
		{
			// No commands to redo
			if (((us->undoCursorHead + us->numCommands) % us->maxHistorySize) == us->undoCursorTail)
			{
				return;
			}

			uint32 offsetToRedo = us->undoCursorTail;
			us->history[offsetToRedo]->execute(us->am);
			us->undoCursorTail = (us->undoCursorTail + 1) % us->maxHistorySize;
		}

		void applyU8Vec4ToChildren(UndoSystemData* us, AnimObjId id, U8Vec4PropType propType)
		{
			auto* newCommand = (ApplyU8Vec4ToChildrenCommand*)g_memory_allocate(sizeof(ApplyU8Vec4ToChildrenCommand));
			new(newCommand)ApplyU8Vec4ToChildrenCommand(id, propType);
			pushAndExecuteCommand(us, newCommand);
		}

		void setU8Vec4Prop(UndoSystemData* us, AnimObjId objId, const glm::u8vec4& oldVec, const glm::u8vec4& newVec, U8Vec4PropType propType)
		{
			auto* newCommand = (ModifyU8Vec4Command*)g_memory_allocate(sizeof(ModifyU8Vec4Command));
			new(newCommand)ModifyU8Vec4Command(objId, oldVec, newVec, propType);
			pushAndExecuteCommand(us, newCommand);
		}

#pragma warning( push )
#pragma warning( disable : 4100 )
		void addNewObjToScene(UndoSystemData* us, const AnimObject& obj)
		{

		}

		void removeObjFromScene(UndoSystemData* us, AnimObjId objId)
		{

		}
#pragma warning( pop )

		// ----------------------------------------
		// Internal Implementations
		// ----------------------------------------
		static void pushAndExecuteCommand(UndoSystemData* us, Command* command)
		{
			// Remove any commands after the current tail
			{
				uint32 numCommandsRemoved = 0;
				for (uint32 i = us->undoCursorTail;
					i != ((us->undoCursorHead + us->numCommands) % us->maxHistorySize);
					i = (i + 1) % us->maxHistorySize)
				{
					if (i == ((us->undoCursorHead + us->numCommands) % us->maxHistorySize)) break;

					us->history[i]->~Command();
					g_memory_free(us->history[i]);
					numCommandsRemoved++;
				}

				// Set the number of commands to the appropriate number now
				us->numCommands -= numCommandsRemoved;
			}

			if (((us->undoCursorTail + 1) % us->maxHistorySize) == us->undoCursorHead)
			{
				us->history[us->undoCursorHead]->~Command();
				g_memory_free(us->history[us->undoCursorHead]);

				us->undoCursorHead = (us->undoCursorHead + 1) % us->maxHistorySize;
				us->numCommands--;
			}

			uint32 offsetToPlaceNewCommand = us->undoCursorTail;
			us->history[offsetToPlaceNewCommand] = command;
			us->numCommands++;
			us->undoCursorTail = (us->undoCursorTail + 1) % us->maxHistorySize;

			us->history[offsetToPlaceNewCommand]->execute(us->am);
		}
	}

	// -------------------------------------
	// Command Implementations
	// -------------------------------------
	void ModifyU8Vec4Command::execute(AnimationManagerData* const am)
	{
		AnimObject* obj = AnimationManager::getMutableObject(am, this->objId);
		if (obj)
		{
			switch (propType)
			{
			case U8Vec4PropType::FillColor:
				obj->_fillColorStart = this->newVec;
				break;
			case U8Vec4PropType::StrokeColor:
				obj->_strokeColorStart = this->newVec;
				break;
			}
			AnimationManager::updateObjectState(am, this->objId);
		}
	}

	void ModifyU8Vec4Command::undo(AnimationManagerData* const am)
	{
		AnimObject* obj = AnimationManager::getMutableObject(am, this->objId);
		if (obj)
		{
			switch (propType)
			{
			case U8Vec4PropType::FillColor:
				obj->_fillColorStart = this->oldVec;
				break;
			case U8Vec4PropType::StrokeColor:
				obj->_strokeColorStart = this->oldVec;
				break;
			}
			AnimationManager::updateObjectState(am, this->objId);
		}
	}

	void ApplyU8Vec4ToChildrenCommand::execute(AnimationManagerData* const am)
	{
		AnimObject* obj = AnimationManager::getMutableObject(am, this->objId);
		if (obj)
		{
			for (auto it = obj->beginBreadthFirst(am); it != obj->end(); ++it)
			{
				AnimObjId childId = *it;
				AnimObject* childObj = AnimationManager::getMutableObject(am, childId);
				if (childObj)
				{
					switch (propType)
					{
					case U8Vec4PropType::FillColor:
						this->oldProps[childId] = childObj->_fillColorStart;
						childObj->_fillColorStart = obj->_fillColorStart;
						break;
					case U8Vec4PropType::StrokeColor:
						this->oldProps[childId] = childObj->_strokeColorStart;
						childObj->_fillColorStart = obj->_strokeColorStart;
						break;
					}
				}
			}
			AnimationManager::updateObjectState(am, this->objId);
		}
	}

	void ApplyU8Vec4ToChildrenCommand::undo(AnimationManagerData* const am)
	{
		AnimObject* obj = AnimationManager::getMutableObject(am, this->objId);
		if (obj)
		{
			for (auto it = obj->beginBreadthFirst(am); it != obj->end(); ++it)
			{
				AnimObjId childId = *it;
				AnimObject* childObj = AnimationManager::getMutableObject(am, childId);
				if (childObj)
				{
					if (auto childColorIter = this->oldProps.find(childId); childColorIter != this->oldProps.end())
					{
						switch (propType)
						{
						case U8Vec4PropType::FillColor:
							childObj->_fillColorStart = childColorIter->second;
							break;
						case U8Vec4PropType::StrokeColor:
							childObj->_strokeColorStart = childColorIter->second;
							break;
						}
					}
				}
			}
			AnimationManager::updateObjectState(am, this->objId);
		}
	}
}