#pragma once

#include <Functions/JSONPath/ASTs/ASTJSONPathMemberAccess.h>
#include <Functions/JSONPath/Generators/IVisitor.h>
#include <Functions/JSONPath/Generators/VisitorStatus.h>

namespace DB
{
template <typename JSONParser>
class VisitorJSONPathMemberAccess : public IVisitor<JSONParser>
{
public:
    VisitorJSONPathMemberAccess(ASTPtr member_access_ptr_)
        : member_access_ptr(member_access_ptr_->as<ASTJSONPathMemberAccess>()) { }

    const char * getName() const override { return "VisitorJSONPathMemberAccess"; }

    VisitorStatus apply(typename JSONParser::Element & element) const override
    {
        typename JSONParser::Element result;
        element.getObject().find(std::string_view(member_access_ptr->member_name), result);
        element = result;
        return VisitorStatus::Ok;
    }

    VisitorStatus visit(typename JSONParser::Element & element) override
    {
        if (!element.isObject())
        {
            this->setExhausted(true);
            return VisitorStatus::Error;
        }
        typename JSONParser::Element result;
        if (!element.getObject().find(std::string_view(member_access_ptr->member_name), result))
        {
            this->setExhausted(true);
            return VisitorStatus::Error;
        }
        apply(element);
        this->setExhausted(true);
        return VisitorStatus::Ok;
    }

    void reinitialize() override { this->setExhausted(false); }

    void updateState() override { }

private:
    ASTJSONPathMemberAccess * member_access_ptr;
};

}
