#ifndef LMMS_AGENT_INCREMENTAL_UNDO_H
#define LMMS_AGENT_INCREMENTAL_UNDO_H

#include <QPointer>
#include <stdexcept>

#include "Engine.h"
#include "ProjectJournal.h"

namespace lmms::agent {

// Register before writing. Both directions use captured values and the existing
// local restoration function, rather than executing commands or reading files.
template <typename Target, typename Before, typename After, typename Setter>
void recordIncrementalValue(Target* object, Before before, After after, Setter setter)
{
	auto* journal = Engine::projectJournal();
	if (!journal || !journal->isIncrementalTransaction())
	{
		return;
	}
	const QPointer<Target> target(object);
	if (!journal->addIncrementalChange({[target] { return !target.isNull(); },
										[target, before, setter] { setter(target.data(), before); },
										[target, after, setter] { setter(target.data(), after); }}))
	{
		throw std::runtime_error("Could not record the local edit inverse.");
	}
}

// For values held by non-QObject parts of an owner, such as mixer channels.
// `present` confirms that the part still exists at its captured identity.
template <typename Owner, typename Present, typename Before, typename After, typename Setter>
void recordIncrementalOwnedValue(Owner* owner, Present present, Before before, After after, Setter setter)
{
	auto* journal = Engine::projectJournal();
	if (!journal || !journal->isIncrementalTransaction())
	{
		return;
	}
	const QPointer<Owner> target(owner);
	const auto valid = [target, present] {
		return !target.isNull() && present(target.data());
	};
	if (!journal->addIncrementalChange({valid,
										[target, before, setter] { setter(target.data(), before); },
										[target, after, setter] { setter(target.data(), after); }}))
	{
		throw std::runtime_error("Could not record the local edit inverse.");
	}
}

} // namespace lmms::agent

#endif // LMMS_AGENT_INCREMENTAL_UNDO_H
