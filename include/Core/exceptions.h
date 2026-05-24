#pragma once
#include <stdexcept>
#include <string>

//exceptions class, this is where all the exceptions are defined

//Missing Component, gets thrown when on of the components of the GameObject is missing a required component
class MissingComponentException : public std::runtime_error {
public:
    MissingComponentException(const std::string& requester, const std::string& missing)
        : std::runtime_error("[" + requester + "] missing required component: " + missing),
          RequesterName(requester),
          ErrorComponentName(missing)
    {}

    const std::string RequesterName;
    const std::string ErrorComponentName;
};


// Duplicated Component, a GameObject generally cannot have 2+ of the same components
class DuplicatedComponentException : public std::runtime_error {
public:
    DuplicatedComponentException(const std::string& requester, const std::string& dupe)
        : std::runtime_error("[" + requester + "] already have the component: " + dupe),
          RequesterName(requester),
          ErrorComponentName(dupe)
    {}

    const std::string RequesterName;
    const std::string ErrorComponentName;
};


// Incompatible exception, means that this component or GameObject cannot work with this other component
class IncompatibleComponentException : public std::runtime_error {
public:
    IncompatibleComponentException(const std::string& requester, const std::string& illegal)
        : std::runtime_error("[" + requester + "] is incompatible with: " + illegal),
          RequesterName(requester),
          ErrorComponentName(illegal)
    {}

    const std::string RequesterName;
    const std::string ErrorComponentName;
};


// Conflicting Component, means the Component can or is having problems with another component
class ConflictingComponentException : public std::runtime_error {
public:
    ConflictingComponentException(const std::string& requester, const std::string& conflict)
        : std::runtime_error("[" + requester + "] is conflicting with: " + conflict),
          RequesterName(requester),
          ErrorComponentName(conflict)
    {}

    const std::string RequesterName;
    const std::string ErrorComponentName;
};


