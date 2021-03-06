/*MT*
    
    MediaTomb - http://www.mediatomb.cc/
    
    script.cc - this file is part of MediaTomb.
    
    Copyright (C) 2005 Gena Batyan <bgeradz@mediatomb.cc>,
                       Sergey 'Jin' Bostandzhyan <jin@mediatomb.cc>
    
    Copyright (C) 2006-2010 Gena Batyan <bgeradz@mediatomb.cc>,
                            Sergey 'Jin' Bostandzhyan <jin@mediatomb.cc>,
                            Leonhard Wimmer <leo@mediatomb.cc>
    
    MediaTomb is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    as published by the Free Software Foundation.
    
    MediaTomb is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    version 2 along with MediaTomb; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
    
    $Id$
*/

/// \file script.cc

#ifdef HAVE_JS

#include "script.h"

#include "config/config_manager.h"
#include "js_functions.h"
#include "metadata/metadata_handler.h"
#include "util/tools.h"
#include <utility>
#ifdef ONLINE_SERVICES
#include "onlineservice/online_service.h"
#endif

#ifdef ATRAILERS
#include "onlineservice/atrailers_content_handler.h"
#endif

static duk_function_list_entry js_global_functions[] = {
    { "print", js_print, DUK_VARARGS },
    { "addCdsObject", js_addCdsObject, 3 },
    { "copyObject", js_copyObject, 1 },
    { "f2i", js_f2i, 1 },
    { "m2i", js_m2i, 1 },
    { "p2i", js_p2i, 1 },
    { "j2i", js_j2i, 1 },
    { nullptr, nullptr, 0 },
};

std::string Script::getProperty(const std::string& name)
{
    std::string ret;
    if (!duk_is_object_coercible(ctx, -1))
        return "";
    duk_get_prop_string(ctx, -1, name.c_str());
    if (duk_is_null_or_undefined(ctx, -1) || !duk_to_string(ctx, -1)) {
        duk_pop(ctx);
        return "";
    }
    ret = duk_get_string(ctx, -1);
    duk_pop(ctx);
    return ret;
}

int Script::getBoolProperty(const std::string& name)
{
    int ret;
    if (!duk_is_object_coercible(ctx, -1))
        return -1;
    duk_get_prop_string(ctx, -1, name.c_str());
    if (duk_is_null_or_undefined(ctx, -1)) {
        duk_pop(ctx);
        return -1;
    }
    ret = duk_to_boolean(ctx, -1);
    duk_pop(ctx);
    return ret;
}

int Script::getIntProperty(const std::string& name, int def)
{
    int ret;
    if (!duk_is_object_coercible(ctx, -1))
        return def;
    duk_get_prop_string(ctx, -1, name.c_str());
    if (duk_is_null_or_undefined(ctx, -1)) {
        duk_pop(ctx);
        return def;
    }
    ret = duk_to_int32(ctx, -1);
    duk_pop(ctx);
    return ret;
}

void Script::setProperty(const std::string& name, const std::string& value)
{
    duk_push_string(ctx, value.c_str());
    duk_put_prop_string(ctx, -2, name.c_str());
}

void Script::setIntProperty(const std::string& name, int value)
{
    duk_push_int(ctx, value);
    duk_put_prop_string(ctx, -2, name.c_str());
}

/* **************** */

Script::Script(const std::shared_ptr<ConfigManager>& config,
    std::shared_ptr<Storage> storage,
    std::shared_ptr<ContentManager> content,
    const std::shared_ptr<Runtime>& runtime, const std::string& name)
    : config(config)
    , storage(std::move(storage))
    , content(std::move(content))
    , runtime(runtime)
    , name(name)
{
    gc_counter = 0;

    this->runtime = runtime;

    /* create a context and associate it with the JS run time */
    Runtime::AutoLock lock(runtime->getMutex());
    ctx = runtime->createContext(name);
    if (!ctx)
        throw std::runtime_error("Scripting: could not initialize js context");

    _p2i = StringConverter::p2i(config);
    _j2i = StringConverter::j2i(config);
    _m2i = StringConverter::m2i(config);
    _f2i = StringConverter::f2i(config);
    _i2i = StringConverter::i2i(config);

    duk_push_thread_stash(ctx, ctx);
    duk_push_pointer(ctx, this);
    duk_put_prop_string(ctx, -2, "this");
    duk_pop(ctx);

    /* initialize contstants */
    duk_push_int(ctx, OBJECT_TYPE_CONTAINER);
    duk_put_global_string(ctx, "OBJECT_TYPE_CONTAINER");
    duk_push_int(ctx, OBJECT_TYPE_ITEM);
    duk_put_global_string(ctx, "OBJECT_TYPE_ITEM");
    duk_push_int(ctx, OBJECT_TYPE_ACTIVE_ITEM);
    duk_put_global_string(ctx, "OBJECT_TYPE_ACTIVE_ITEM");
    duk_push_int(ctx, OBJECT_TYPE_ITEM_EXTERNAL_URL);
    duk_put_global_string(ctx, "OBJECT_TYPE_ITEM_EXTERNAL_URL");
    duk_push_int(ctx, OBJECT_TYPE_ITEM_INTERNAL_URL);
    duk_put_global_string(ctx, "OBJECT_TYPE_ITEM_INTERNAL_URL");
#ifdef ONLINE_SERVICES
    duk_push_int(ctx, static_cast<int>(OS_None));
    duk_put_global_string(ctx, "ONLINE_SERVICE_NONE");
    duk_push_int(ctx, -1);
    duk_put_global_string(ctx, "ONLINE_SERVICE_YOUTUBE");

#ifdef ATRAILERS
    duk_push_int(ctx, static_cast<int>(OS_ATrailers));
    duk_put_global_string(ctx, "ONLINE_SERVICE_APPLE_TRAILERS");
    duk_push_string(ctx, ATRAILERS_AUXDATA_POST_DATE);
    duk_put_global_string(ctx, "APPLE_TRAILERS_AUXDATA_POST_DATE");
#else
    duk_push_int(ctx, -1);
    duk_put_global_string(ctx, "ONLINE_SERVICE_APPLE_TRAILERS");
#endif //ATRAILERS

#ifdef SOPCAST
    duk_push_int(ctx, static_cast<int>(OS_SopCast));
    duk_put_global_string(ctx, "ONLINE_SERVICE_SOPCAST");
#else
    duk_push_int(ctx, -1);
    duk_put_global_string(ctx, "ONLINE_SERVICE_SOPCAST");
#endif //SOPCAST

#else // ONLINE SERVICES
    duk_push_int(ctx, 0);
    duk_put_global_string(ctx, "ONLINE_SERVICE_NONE");
    duk_push_int(ctx, -1);
    duk_put_global_string(ctx, "ONLINE_SERVICE_YOUTUBE");
    duk_push_int(ctx, -1);
    duk_put_global_string(ctx, "ONLINE_SERVICE_APPLE_TRAILERS");
    duk_push_int(ctx, -1);
    duk_put_global_string(ctx, "ONLINE_SERVICE_SOPCAST");
#endif //ONLINE_SERVICES

    for (int i = 0; i < M_MAX; i++) {
        duk_push_string(ctx, MT_KEYS[i].upnp);
        duk_put_global_string(ctx, MT_KEYS[i].sym);
    }

    for (int i = 0; i < R_MAX; i++) {
        duk_push_string(ctx, RES_KEYS[i].upnp);
        duk_put_global_string(ctx, RES_KEYS[i].sym);
    }

    duk_push_string(ctx, UPNP_DEFAULT_CLASS_MUSIC_ALBUM);
    duk_put_global_string(ctx, "UPNP_CLASS_CONTAINER_MUSIC_ALBUM");
    duk_push_string(ctx, UPNP_DEFAULT_CLASS_MUSIC_ARTIST);
    duk_put_global_string(ctx, "UPNP_CLASS_CONTAINER_MUSIC_ARTIST");
    duk_push_string(ctx, UPNP_DEFAULT_CLASS_MUSIC_GENRE);
    duk_put_global_string(ctx, "UPNP_CLASS_CONTAINER_MUSIC_GENRE");
    duk_push_string(ctx, UPNP_DEFAULT_CLASS_MUSIC_COMPOSER);
    duk_put_global_string(ctx, "UPNP_CLASS_CONTAINER_MUSIC_COMPOSER");
    duk_push_string(ctx, UPNP_DEFAULT_CLASS_MUSIC_CONDUCTOR);
    duk_put_global_string(ctx, "UPNP_CLASS_CONTAINER_MUSIC_CONDUCTOR");
    duk_push_string(ctx, UPNP_DEFAULT_CLASS_MUSIC_ORCHESTRA);
    duk_put_global_string(ctx, "UPNP_CLASS_CONTAINER_MUSIC_ORCHESTRA");
    duk_push_string(ctx, UPNP_DEFAULT_CLASS_CONTAINER);
    duk_put_global_string(ctx, "UPNP_CLASS_CONTAINER");
    duk_push_string(ctx, UPNP_DEFAULT_CLASS_ITEM);
    duk_put_global_string(ctx, "UPNP_CLASS_ITEM");
    duk_push_string(ctx, UPNP_DEFAULT_CLASS_MUSIC_TRACK);
    duk_put_global_string(ctx, "UPNP_CLASS_ITEM_MUSIC_TRACK");
    duk_push_string(ctx, UPNP_DEFAULT_CLASS_VIDEO_ITEM);
    duk_put_global_string(ctx, "UPNP_CLASS_CONTAINER_ITEM_VIDEO");
    duk_push_string(ctx, UPNP_DEFAULT_CLASS_IMAGE_ITEM);
    duk_put_global_string(ctx, "UPNP_CLASS_CONTAINER_ITEM_IMAGE");
    duk_push_string(ctx, UPNP_DEFAULT_CLASS_PLAYLIST_CONTAINER);
    duk_put_global_string(ctx, "UPNP_CLASS_PLAYLIST_CONTAINER");

    defineFunctions(js_global_functions);

    std::string common_scr_path = config->getOption(CFG_IMPORT_SCRIPTING_COMMON_SCRIPT);

    if (!string_ok(common_scr_path)) {
        log_js("Common script disabled in configuration");
    } else {
        try {
            _load(common_scr_path);
            _execute();
        } catch (const std::runtime_error& e) {
            log_js("Unable to load {}: {}", common_scr_path.c_str(),
                e.what());
        }
    }
}

Script::~Script()
{
    runtime->destroyContext(name);
}

Script* Script::getContextScript(duk_context* ctx)
{
    duk_push_thread_stash(ctx, ctx);
    duk_get_prop_string(ctx, -1, "this");
    auto self = static_cast<Script*>(duk_get_pointer(ctx, -1));
    duk_pop_2(ctx);
    if (self == nullptr) {
        log_debug("Could not retrieve class instance from global object");
        duk_error(ctx, DUK_ERR_ERROR, "Could not retrieve current script from stash");
    }
    return self;
}

void Script::defineFunction(const std::string& name, duk_c_function function, uint32_t numParams)
{
    duk_push_c_function(ctx, function, numParams);
    duk_put_global_string(ctx, name.c_str());
}

void Script::defineFunctions(duk_function_list_entry* functions)
{
    duk_push_global_object(ctx);
    duk_put_function_list(ctx, -1, functions);
    duk_pop(ctx);
}

void Script::_load(const std::string& scriptPath)
{
    std::string scriptText = readTextFile(scriptPath);

    if (!string_ok(scriptText))
        throw std::runtime_error("empty script");

    auto j2i = StringConverter::j2i(config);
    try {
        scriptText = j2i->convert(scriptText, true);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(std::string { "Failed to convert import script:" } + e.what());
    }

    duk_push_string(ctx, scriptPath.c_str());
    if (duk_pcompile_lstring_filename(ctx, 0, scriptText.c_str(), scriptText.length()) != 0)
        throw std::runtime_error("Scripting: failed to compile " + scriptPath);
}

void Script::load(const std::string& scriptPath)
{
    Runtime::AutoLock lock(runtime->getMutex());
    duk_push_thread_stash(ctx, ctx);
    _load(scriptPath);
    duk_put_prop_string(ctx, -2, "script");
    duk_pop(ctx);
}

void Script::_execute()
{
    if (duk_pcall(ctx, 0) != DUK_EXEC_SUCCESS) {
        log_error("Failed to execute script: {}", duk_safe_to_string(ctx, -1));
        throw std::runtime_error("Script: failed to execute script");
    }
    duk_pop(ctx);
}

void Script::execute()
{
    Runtime::AutoLock lock(runtime->getMutex());
    duk_push_thread_stash(ctx, ctx);
    duk_get_prop_string(ctx, -1, "script");
    duk_remove(ctx, -2);
    _execute();
}

std::shared_ptr<CdsObject> Script::dukObject2cdsObject(const std::shared_ptr<CdsObject>& pcd)
{
    std::string val;
    int objectType;
    int b;
    int i;
    std::unique_ptr<StringConverter> sc;

    if (this->whoami() == S_PLAYLIST) {
        sc = StringConverter::p2i(config);
    } else
        sc = StringConverter::i2i(config);

    objectType = getIntProperty("objectType", -1);
    if (objectType == -1) {
        log_error("missing objectType property");
        return nullptr;
    }

    auto obj = CdsObject::createObject(storage, objectType);
    objectType = obj->getObjectType(); // this is important, because the
    // type will be changed appropriately
    // by the create function

    // CdsObject
    obj->setVirtual(true); // JS creates only virtual objects

    i = getIntProperty("id", INVALID_OBJECT_ID);
    if (i != INVALID_OBJECT_ID)
        obj->setID(i);
    i = getIntProperty("refID", INVALID_OBJECT_ID);
    if (i != INVALID_OBJECT_ID)
        obj->setRefID(i);
    i = getIntProperty("parentID", INVALID_OBJECT_ID);
    if (i != INVALID_OBJECT_ID)
        obj->setParentID(i);

    val = getProperty("title");
    if (!val.empty()) {
        val = sc->convert(val);
        obj->setTitle(val);
    } else {
        if (pcd != nullptr)
            obj->setTitle(pcd->getTitle());
    }

    val = getProperty("upnpclass");
    if (!val.empty()) {
        val = sc->convert(val);
        obj->setClass(val);
    } else {
        if (pcd != nullptr)
            obj->setClass(pcd->getClass());
    }

    b = getBoolProperty("restricted");
    if (b >= 0)
        obj->setRestricted(b);

    duk_get_prop_string(ctx, -1, "meta");
    if (duk_is_object(ctx, -1)) {
        duk_to_object(ctx, -1);
        /// \todo: only metadata enumerated in MT_KEYS is taken
        for (int i = 0; i < M_MAX; i++) {
            val = getProperty(MT_KEYS[i].upnp);
            if (!val.empty()) {
                if (i == M_TRACKNUMBER) {
                    int j = std::stoi(val);
                    if (j > 0) {
                        obj->setMetadata(MT_KEYS[i].upnp, val);
                        std::static_pointer_cast<CdsItem>(obj)->setTrackNumber(j);
                    } else
                        std::static_pointer_cast<CdsItem>(obj)->setTrackNumber(0);
                } else {
                    val = sc->convert(val);
                    obj->setMetadata(MT_KEYS[i].upnp, val);
                }
            }
        }
    }
    duk_pop(ctx);

    // stuff that has not been exported to js
    if (pcd != nullptr) {
        obj->setFlags(pcd->getFlags());
        obj->setResources(pcd->getResources());
        obj->setAuxData(pcd->getAuxData());
    }

    // CdsItem
    if (IS_CDS_ITEM(objectType)) {
        auto item = std::static_pointer_cast<CdsItem>(obj);
        std::shared_ptr<CdsItem> pcd_item;

        if (pcd != nullptr)
            pcd_item = std::static_pointer_cast<CdsItem>(pcd);

        val = getProperty("mimetype");
        if (!val.empty()) {
            val = sc->convert(val);
            item->setMimeType(val);
        } else {
            if (pcd != nullptr)
                item->setMimeType(pcd_item->getMimeType());
        }

        val = getProperty("serviceID");
        if (!val.empty()) {
            val = sc->convert(val);
            item->setServiceID(val);
        }

        /// \todo check what this is doing here, wasn't it already handled
        /// in the MT_KEYS loop?
        val = getProperty("description");
        if (!val.empty()) {
            val = sc->convert(val);
            item->setMetadata(MetadataHandler::getMetaFieldName(M_DESCRIPTION), val);
        } else {
            if (pcd != nullptr)
                item->setMetadata(MetadataHandler::getMetaFieldName(M_DESCRIPTION),
                    pcd_item->getMetadata(MetadataHandler::getMetaFieldName(M_DESCRIPTION)));
        }
        if (this->whoami() == S_PLAYLIST) {
            item->setTrackNumber(getIntProperty("playlistOrder", 0));
        }

        // location must not be touched by character conversion!
        fs::path location = getProperty("location");
        if (string_ok(location))
            obj->setLocation(location);
        else {
            if (pcd != nullptr)
                obj->setLocation(pcd->getLocation());
        }

        if (IS_CDS_ACTIVE_ITEM(objectType)) {
            auto aitem = std::static_pointer_cast<CdsActiveItem>(obj);
            std::shared_ptr<CdsActiveItem> pcd_aitem;
            if (pcd != nullptr)
                pcd_aitem = std::static_pointer_cast<CdsActiveItem>(pcd);

            /// \todo what about character conversion for action and state fields?
            val = getProperty("action");
            if (!val.empty())
                aitem->setAction(val);
            else {
                if (pcd != nullptr)
                    aitem->setAction(pcd_aitem->getAction());
            }

            val = getProperty("state");
            if (!val.empty())
                aitem->setState(val);
            else {
                if (pcd != nullptr)
                    aitem->setState(pcd_aitem->getState());
            }
        }

        if (IS_CDS_ITEM_EXTERNAL_URL(objectType)) {
            std::string protocolInfo;

            obj->setRestricted(true);
            auto item = std::static_pointer_cast<CdsItemExternalURL>(obj);
            val = getProperty("protocol");
            if (!val.empty()) {
                val = sc->convert(val);
                protocolInfo = renderProtocolInfo(item->getMimeType(), val);
            } else {
                protocolInfo = renderProtocolInfo(item->getMimeType(), PROTOCOL);
            }

            if (item->getResourceCount() == 0) {
                auto resource = std::make_shared<CdsResource>(CH_DEFAULT);
                resource->addAttribute(MetadataHandler::getResAttrName(
                                           R_PROTOCOLINFO),
                    protocolInfo);

                item->addResource(resource);
            }
        }
    }

    // CdsDirectory
    if (IS_CDS_CONTAINER(objectType)) {
        auto cont = std::static_pointer_cast<CdsContainer>(obj);
        i = getIntProperty("updateID", -1);
        if (i >= 0)
            cont->setUpdateID(i);

        b = getBoolProperty("searchable");
        if (b >= 0)
            cont->setSearchable(b);
    }

    return obj;
}

void Script::cdsObject2dukObject(const std::shared_ptr<CdsObject>& obj)
{
    std::string val;
    int i;

    duk_push_object(ctx);

    int objectType = obj->getObjectType();

    // CdsObject
    setIntProperty("objectType", objectType);

    i = obj->getID();

    if (i != INVALID_OBJECT_ID)
        setIntProperty("id", i);

    i = obj->getParentID();
    if (i != INVALID_OBJECT_ID)
        setIntProperty("parentID", i);

    val = obj->getTitle();
    if (!val.empty())
        setProperty("title", val);

    val = obj->getClass();
    if (!val.empty())
        setProperty("upnpclass", val);

    val = obj->getLocation();
    if (!val.empty())
        setProperty("location", val);

    setIntProperty("mtime", static_cast<int>(obj->getMTime()));
    setIntProperty("sizeOnDisk", static_cast<int>(obj->getSizeOnDisk()));

    // TODO: boolean type
    i = obj->isRestricted();
    setIntProperty("restricted", i);

    if (obj->getFlag(OBJECT_FLAG_OGG_THEORA))
        setIntProperty("theora", 1);
    else
        setIntProperty("theora", 0);

#ifdef ONLINE_SERVICES
    if (obj->getFlag(OBJECT_FLAG_ONLINE_SERVICE)) {
        auto service = static_cast<service_type_t>(std::stoi(obj->getAuxData(ONLINE_SERVICE_AUX_ID)));
        setIntProperty("onlineservice", static_cast<int>(service));
    } else
#endif
        setIntProperty("onlineservice", 0);

    // setting metadata
    {
        duk_push_object(ctx);
        // stack: js meta_js

        auto meta = obj->getMetadata();
        for (const auto& it : meta) {
            setProperty(it.first, it.second);
        }

        if (std::static_pointer_cast<CdsItem>(obj)->getTrackNumber() > 0)
            setProperty(MetadataHandler::getMetaFieldName(M_TRACKNUMBER), std::to_string(std::static_pointer_cast<CdsItem>(obj)->getTrackNumber()));

        duk_put_prop_string(ctx, -2, "meta");
        // stack: js
    }

    // setting auxdata
    {
        duk_push_object(ctx);
        // stack: js aux_js

        auto aux = obj->getAuxData();

#ifdef HAVE_ATRAILERS
        auto tmp = obj->getAuxData(ATRAILERS_AUXDATA_POST_DATE);
        if (string_ok(tmp))
            aux[ATRAILERS_AUXDATA_POST_DATE] = tmp;
#endif

        for (const auto& it : aux) {
            setProperty(it.first, it.second);
        }

        duk_put_prop_string(ctx, -2, "aux");
        // stack: js
    }

    // setting resource
    {
        duk_push_object(ctx);
        // stack: js res_js

        if (obj->getResourceCount() > 0) {
            auto res = obj->getResource(0);
            auto attributes = res->getAttributes();
            for (const auto& attribute : attributes) {
                setProperty(attribute.first, attribute.second);
            }
        }

        duk_put_prop_string(ctx, -2, "res");
        // stack: js
    }

    // CdsItem
    if (IS_CDS_ITEM(objectType)) {
        auto item = std::static_pointer_cast<CdsItem>(obj);
        val = item->getMimeType();
        if (!val.empty())
            setProperty("mimetype", val);

        val = item->getServiceID();
        if (!val.empty())
            setProperty("serviceID", val);

        if (IS_CDS_ACTIVE_ITEM(objectType)) {
            auto aitem = std::static_pointer_cast<CdsActiveItem>(obj);
            val = aitem->getAction();
            if (!val.empty())
                setProperty("action", val);
            val = aitem->getState();
            if (!val.empty())
                setProperty("state", val);
        }
    }

    // CdsDirectory
    if (IS_CDS_CONTAINER(objectType)) {
        auto cont = std::static_pointer_cast<CdsContainer>(obj);
        // TODO: boolean type, hide updateID
        i = cont->getUpdateID();
        setIntProperty("updateID", i);

        i = cont->isSearchable();
        setIntProperty("searchable", i);
    }
}

std::string Script::convertToCharset(const std::string& str, charset_convert_t chr)
{
    switch (chr) {
    case P2I:
        return _p2i->convert(str);
    case M2I:
        return _m2i->convert(str);
    case F2I:
        return _f2i->convert(str);
    case J2I:
        return _j2i->convert(str);
    default:
        return _i2i->convert(str);
    }
}

std::shared_ptr<CdsObject> Script::getProcessedObject()
{
    return processed;
}

#endif // HAVE_JS
