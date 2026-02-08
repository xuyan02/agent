#include "tool/tool_spec.h"

namespace agent {

TypeSpec::Kind TypeSpec::kind() const {
  if (!impl_)
    return Kind::kString;
  return impl_->kind();
}

const TypeSpec::String* TypeSpec::AsString() const {
  if (kind() != Kind::kString)
    return nullptr;
  return reinterpret_cast<const String*>(impl_.get());
}

const TypeSpec::Number* TypeSpec::AsNumber() const {
  if (kind() != Kind::kNumber)
    return nullptr;
  return reinterpret_cast<const Number*>(impl_.get());
}

const TypeSpec::Integer* TypeSpec::AsInteger() const {
  if (kind() != Kind::kInteger)
    return nullptr;
  return reinterpret_cast<const Integer*>(impl_.get());
}

const TypeSpec::Boolean* TypeSpec::AsBoolean() const {
  if (kind() != Kind::kBoolean)
    return nullptr;
  return reinterpret_cast<const Boolean*>(impl_.get());
}

const TypeSpec::Object* TypeSpec::AsObject() const {
  if (kind() != Kind::kObject)
    return nullptr;
  return reinterpret_cast<const Object*>(impl_.get());
}

const TypeSpec::Array* TypeSpec::AsArray() const {
  if (kind() != Kind::kArray)
    return nullptr;
  return reinterpret_cast<const Array*>(impl_.get());
}

}  // namespace agent
