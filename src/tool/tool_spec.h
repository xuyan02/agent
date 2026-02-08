#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace agent {

class FieldSpec;

class TypeSpecImpl;
class TypeSpecImplString;
class TypeSpecImplNumber;
class TypeSpecImplInteger;
class TypeSpecImplBoolean;
class TypeSpecImplObject;
class TypeSpecImplArray;

class TypeSpec {
 public:
  enum class Kind {
    kString,
    kNumber,
    kInteger,
    kBoolean,
    kObject,
    kArray,
  };

  using String = TypeSpecImplString;
  using Number = TypeSpecImplNumber;
  using Integer = TypeSpecImplInteger;
  using Boolean = TypeSpecImplBoolean;
  using Object = TypeSpecImplObject;
  using Array = TypeSpecImplArray;

  TypeSpec() = delete;

  TypeSpec(TypeSpec&&) noexcept = default;
  TypeSpec& operator=(TypeSpec&&) noexcept = default;

  TypeSpec(const TypeSpec&) = delete;
  TypeSpec& operator=(const TypeSpec&) = delete;

  explicit operator bool() const { return impl_ != nullptr; }

  Kind kind() const;

  const String* AsString() const;
  const Number* AsNumber() const;
  const Integer* AsInteger() const;
  const Boolean* AsBoolean() const;
  const Object* AsObject() const;
  const Array* AsArray() const;

 public:
  template <typename Impl>
  static TypeSpec FromImpl(std::unique_ptr<Impl> impl) {
    return TypeSpec(std::unique_ptr<const TypeSpecImpl>(std::move(impl)));
  }

 private:
  explicit TypeSpec(std::unique_ptr<const TypeSpecImpl> impl) : impl_(std::move(impl)) {}

  std::unique_ptr<const TypeSpecImpl> impl_;
};

class TypeSpecImpl {
 public:
  virtual ~TypeSpecImpl() = default;

  TypeSpecImpl(const TypeSpecImpl&) = delete;
  TypeSpecImpl& operator=(const TypeSpecImpl&) = delete;

  virtual TypeSpec::Kind kind() const = 0;

 protected:
  TypeSpecImpl() = default;
};

// Concrete impls + per-kind builders.

class TypeSpecImplString : public TypeSpecImpl {
 public:
  class Builder;

  TypeSpec::Kind kind() const override { return TypeSpec::Kind::kString; }

  std::vector<std::string> enum_values;
};

class TypeSpecImplString::Builder {
 public:
  Builder& AddEnumValue(std::string value) {
    enum_values_.push_back(std::move(value));
    return *this;
  }

  TypeSpec Build() && {
    auto impl = std::make_unique<TypeSpecImplString>();
    impl->enum_values = std::move(enum_values_);
    return TypeSpec::FromImpl(std::move(impl));
  }

 private:
  std::vector<std::string> enum_values_;
};

class TypeSpecImplNumber : public TypeSpecImpl {
 public:
  class Builder;

  TypeSpec::Kind kind() const override { return TypeSpec::Kind::kNumber; }
};

class TypeSpecImplNumber::Builder {
 public:
  TypeSpec Build() && { return TypeSpec::FromImpl(std::make_unique<TypeSpecImplNumber>()); }
};

class TypeSpecImplInteger : public TypeSpecImpl {
 public:
  class Builder;

  TypeSpec::Kind kind() const override { return TypeSpec::Kind::kInteger; }
};

class TypeSpecImplInteger::Builder {
 public:
  TypeSpec Build() && { return TypeSpec::FromImpl(std::make_unique<TypeSpecImplInteger>()); }
};

class TypeSpecImplBoolean : public TypeSpecImpl {
 public:
  class Builder;

  TypeSpec::Kind kind() const override { return TypeSpec::Kind::kBoolean; }
};

class TypeSpecImplBoolean::Builder {
 public:
  TypeSpec Build() && { return TypeSpec::FromImpl(std::make_unique<TypeSpecImplBoolean>()); }
};

// Object types need FieldSpec, so the impl/builder is defined after FieldSpec.

class TypeSpecImplArray : public TypeSpecImpl {
 public:
  class Builder;

  TypeSpec::Kind kind() const override { return TypeSpec::Kind::kArray; }

  TypeSpec items;

  TypeSpecImplArray() : items(TypeSpecImplString::Builder().Build()) {}
};

class TypeSpecImplArray::Builder {
 public:
  Builder& SetItems(TypeSpec items) {
    items_ = std::move(items);
    return *this;
  }

  TypeSpec Build() && {
    auto impl = std::make_unique<TypeSpecImplArray>();
    if (items_)
      impl->items = std::move(items_);
    return TypeSpec::FromImpl(std::move(impl));
  }

 private:
  TypeSpec items_ = TypeSpecImplString::Builder().Build();
};

class FieldSpec {
 public:
  class Builder;

  FieldSpec(FieldSpec&&) noexcept = default;
  FieldSpec& operator=(FieldSpec&&) noexcept = default;

  FieldSpec(const FieldSpec&) = delete;
  FieldSpec& operator=(const FieldSpec&) = delete;

  const std::string& name() const { return name_; }
  const std::string& description() const { return description_; }
  bool required() const { return required_; }
  const TypeSpec& type() const { return type_; }

 private:
  friend class Builder;

  FieldSpec() = delete;

  FieldSpec(std::string name,
            std::string description,
            bool required,
            TypeSpec type)
      : name_(std::move(name)),
        description_(std::move(description)),
        required_(required),
        type_(std::move(type)) {}

  std::string name_;
  std::string description_;
  bool required_ = true;
  TypeSpec type_;
};

class TypeSpecImplObject : public TypeSpecImpl {
 public:
  class Builder;

  TypeSpec::Kind kind() const override { return TypeSpec::Kind::kObject; }

  std::vector<FieldSpec> properties;
  bool additional_properties = false;
};

class TypeSpecImplObject::Builder {
 public:
  Builder& AddProperty(FieldSpec field) {
    properties_.push_back(std::move(field));
    return *this;
  }

  Builder& SetAdditionalProperties(bool additional_properties) {
    additional_properties_ = additional_properties;
    return *this;
  }

  TypeSpec Build() && {
    auto impl = std::make_unique<TypeSpecImplObject>();
    impl->properties = std::move(properties_);
    impl->additional_properties = additional_properties_;
    return TypeSpec::FromImpl(std::move(impl));
  }

 private:
  std::vector<FieldSpec> properties_;
  bool additional_properties_ = false;
};

class FunctionSpec {
 public:
  class Builder;

  FunctionSpec(FunctionSpec&&) noexcept = default;
  FunctionSpec& operator=(FunctionSpec&&) noexcept = default;

  FunctionSpec(const FunctionSpec&) = delete;
  FunctionSpec& operator=(const FunctionSpec&) = delete;

  const std::string& name() const { return name_; }
  const std::string& description() const { return description_; }
  const std::vector<FieldSpec>& params() const { return params_; }

 private:
  friend class Builder;

  FunctionSpec() = delete;

  FunctionSpec(std::string name, std::string description, std::vector<FieldSpec> params)
      : name_(std::move(name)),
        description_(std::move(description)),
        params_(std::move(params)) {}

  std::string name_;
  std::string description_;
  std::vector<FieldSpec> params_;
};

class ToolSpec {
 public:
  class Builder;

  ToolSpec(ToolSpec&&) noexcept = default;
  ToolSpec& operator=(ToolSpec&&) noexcept = default;

  ToolSpec(const ToolSpec&) = delete;
  ToolSpec& operator=(const ToolSpec&) = delete;

  const std::string& name() const { return name_; }
  const std::string& description() const { return description_; }
  const std::vector<FunctionSpec>& functions() const { return functions_; }

 private:
  friend class Builder;

  ToolSpec() = delete;

  ToolSpec(std::string name, std::string description, std::vector<FunctionSpec> functions)
      : name_(std::move(name)),
        description_(std::move(description)),
        functions_(std::move(functions)) {}

  std::string name_;
  std::string description_;
  std::vector<FunctionSpec> functions_;
};

// Builders


class FieldSpec::Builder {
 public:
  Builder() : type_(TypeSpecImplString::Builder().Build()) {}
  Builder& SetName(std::string name) {
    name_ = std::move(name);
    return *this;
  }

  Builder& SetDescription(std::string description) {
    description_ = std::move(description);
    return *this;
  }

  Builder& SetRequired(bool required) {
    required_ = required;
    return *this;
  }

  Builder& SetType(TypeSpec type) {
    type_ = std::move(type);
    return *this;
  }

  FieldSpec Build() && {
    return FieldSpec(std::move(name_), std::move(description_), required_, std::move(type_));
  }

 private:
  std::string name_;
  std::string description_;
  bool required_ = true;
  TypeSpec type_;
};

class FunctionSpec::Builder {
 public:
  Builder& SetName(std::string name) {
    name_ = std::move(name);
    return *this;
  }

  Builder& SetDescription(std::string description) {
    description_ = std::move(description);
    return *this;
  }

  Builder& AddParam(FieldSpec param) {
    params_.push_back(std::move(param));
    return *this;
  }

  FunctionSpec Build() && {
    return FunctionSpec(std::move(name_), std::move(description_), std::move(params_));
  }

 private:
  std::string name_;
  std::string description_;
  std::vector<FieldSpec> params_;
};

class ToolSpec::Builder {
 public:
  Builder& SetName(std::string name) {
    name_ = std::move(name);
    return *this;
  }

  Builder& SetDescription(std::string description) {
    description_ = std::move(description);
    return *this;
  }

  Builder& AddFunction(FunctionSpec function) {
    functions_.push_back(std::move(function));
    return *this;
  }

  ToolSpec Build() && {
    return ToolSpec(std::move(name_), std::move(description_), std::move(functions_));
  }

 private:
  std::string name_;
  std::string description_;
  std::vector<FunctionSpec> functions_;
};

}  // namespace agent
