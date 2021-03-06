/**
 * @file libcomp/src/Object.cpp
 * @ingroup libcomp
 *
 * @author COMP Omega <compomega@tutanota.com>
 *
 * @brief Base class for an object generated by the object generator (objgen).
 *
 * This file is part of the COMP_hack Library (libcomp).
 *
 * Copyright (C) 2012-2018 COMP_hack Team <compomega@tutanota.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Object.h"

// libcomp Includes
#include <Packet.h>
#include <PacketStream.h>
#include <ReadOnlyPacket.h>
#include <ScriptEngine.h>

using namespace libcomp;

Object::Object()
{
}

Object::Object(const Object& other)
{
    (void)other;
}

Object::~Object()
{
}

bool Object::LoadPacket(libcomp::ReadOnlyPacket& p, bool flat)
{
    ReadOnlyPacketStream buffer(p);
    std::istream in(&buffer);

    auto startingPos = in.tellg();
    bool success = Load(in, flat);
    if(success)
    {
        //Fast forward the packet
        auto newPos = in.tellg();
        p.Skip(static_cast<uint32_t>(newPos - startingPos));
    }
    return success;
}

bool Object::SavePacket(libcomp::Packet& p, bool flat) const
{
    PacketStream buffer(p);
    std::ostream out(&buffer);

    return Save(out, flat);
}

const tinyxml2::XMLElement*
    Object::GetXmlChild(const tinyxml2::XMLElement& root, const std::string name) const
{
    auto children = GetXmlChildren(root, name);
    return children.size() > 0 ? children.front() : nullptr;
}

const std::list<const tinyxml2::XMLElement*>
    Object::GetXmlChildren(const tinyxml2::XMLElement& root, const std::string name) const
{
    std::list<const tinyxml2::XMLElement*> retval;

    const tinyxml2::XMLElement *cMember = root.FirstChildElement();
    while(nullptr != cMember)
    {
        if(name == cMember->Name())
        {
            retval.push_back(cMember);
        }

        cMember = cMember->NextSiblingElement();
    }
    return retval;
}

std::unordered_map<std::string, const tinyxml2::XMLElement*>
    Object::GetXmlMembers(const tinyxml2::XMLElement& root) const
{
    std::unordered_map<std::string, const tinyxml2::XMLElement*> members;

    const tinyxml2::XMLElement *pMember = root.FirstChildElement("member");

    while(nullptr != pMember)
    {
        const char *szName = pMember->Attribute("name");

        if(nullptr != szName)
        {
            members[szName] = pMember;
        }

        pMember = pMember->NextSiblingElement("member");
    }

    return members;
}

std::string Object::GetXmlText(const tinyxml2::XMLElement& root) const
{
    std::string value;

    const tinyxml2::XMLNode *pChild = root.FirstChild();

    if(nullptr != pChild)
    {
        const tinyxml2::XMLText *pText = pChild->ToText();

        if(nullptr != pText)
        {
            const char *szText = pText->Value();

            if(nullptr != szText)
            {
                value = szText;
            }
        }
    }

    return value;
}

std::list<std::shared_ptr<Object>> Object::LoadBinaryData(
    std::istream& stream,
    const std::function<std::shared_ptr<Object>()>& objectAllocator)
{
    std::list<std::shared_ptr<Object>> objects;

    uint16_t objectCount;
    uint16_t dynamicSizeCount;

    stream.read(reinterpret_cast<char*>(&objectCount),
        sizeof(objectCount));

    if(!stream.good())
    {
        return {};
    }

    stream.read(reinterpret_cast<char*>(&dynamicSizeCount),
        sizeof(dynamicSizeCount));

    if(!stream.good())
    {
        return {};
    }

    ObjectInStream objectStream(stream);

    for(uint16_t i = 0; i < objectCount; ++i)
    {
        for(uint16_t j = 0; j < dynamicSizeCount; ++j)
        {
            uint16_t dyanmicSize;

            stream.read(reinterpret_cast<char*>(&dyanmicSize),
                sizeof(dyanmicSize));

            if(!stream.good())
            {
                return {};
            }

            objectStream.dynamicSizes.push_back(dyanmicSize);
        }
    }

    for(uint16_t i = 0; i < objectCount; ++i)
    {
        std::shared_ptr<Object> obj(objectAllocator());

        if(!obj->Load(objectStream) || !stream.good())
        {
            return {};
        }

        objects.push_back(obj);
    }

    return objects;
}

bool Object::SaveBinaryData(std::ostream& stream,
    const std::list<std::shared_ptr<Object>>& objs)
{
    if(objs.empty())
    {
        return false;
    }

    uint16_t dynamicSizeCount = objs.front()->GetDynamicSizeCount();

    for(auto obj : objs)
    {
        if(obj->GetDynamicSizeCount() != dynamicSizeCount)
        {
            return false;
        }
    }

    uint16_t objectCount = static_cast<uint16_t>(objs.size());

    stream.write(reinterpret_cast<char*>(&objectCount),
        sizeof(objectCount));

    if(!stream.good())
    {
        return false;
    }

    stream.write(reinterpret_cast<char*>(&dynamicSizeCount),
        sizeof(dynamicSizeCount));

    if(!stream.good())
    {
        return false;
    }

    ObjectOutStream objectStream(stream);

    // Save the position of this data to come back and write it out.
    auto dyanmicSizePosition = stream.tellp();

    for(uint16_t i = 0; i < objectCount; ++i)
    {
        for(uint16_t j = 0; j < dynamicSizeCount; ++j)
        {
            uint16_t dyanmicSize = 0;

            stream.write(reinterpret_cast<char*>(&dyanmicSize),
                sizeof(dyanmicSize));

            if(!stream.good())
            {
                return false;
            }
        }
    }

    for(auto obj : objs)
    {
        if(!obj->Save(objectStream) || !stream.good())
        {
            return false;
        }
    }

    // Go back and write the dynamic sizes.
    stream.seekp(dyanmicSizePosition);

    if(!stream.good())
    {
        return false;
    }

    for(uint16_t i = 0; i < objectCount; ++i)
    {
        for(uint16_t j = 0; j < dynamicSizeCount; ++j)
        {
            uint16_t dyanmicSize = objectStream.dynamicSizes.front();
            objectStream.dynamicSizes.pop_front();

            stream.write(reinterpret_cast<char*>(&dyanmicSize),
                sizeof(dyanmicSize));

            if(!stream.good())
            {
                return false;
            }
        }
    }

    return true;
}

std::string Object::GetXml() const
{
    tinyxml2::XMLDocument doc;

    tinyxml2::XMLElement *pRoot = doc.NewElement("objects");
    doc.InsertEndChild(pRoot);

    if(!Save(doc, *pRoot))
    {
        return {};
    }

    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);

    return printer.CStr();
}

bool Object::SkipPadding(std::istream& stream, uint8_t count)
{
    //stream.ignore(count);
    stream.seekg(count, std::istream::cur);

    return stream.good();
}

bool Object::WritePadding(std::ostream& stream, uint8_t count) const
{
    uint8_t byte = 0;
    for(uint8_t i = 0; i < count; i++)
    {
        stream.write(reinterpret_cast<const char*>(&byte),
            sizeof(byte));
    }

    return stream.good();
}

namespace libcomp
{
    template<>
    ScriptEngine& ScriptEngine::Using<Object>()
    {
        if(!BindingExists("Object"))
        {
            Sqrat::Class<Object, Sqrat::NoConstructor<Object>> binding(
                mVM, "Object");
            Bind<Object>("Object", binding);

            binding
                .Func("IsValid", &Object::IsValid)
                ; // Last call to binding
        }

        return *this;
    }
}
