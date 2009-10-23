/*
Copyright (c) 2009 Sony Pictures Imageworks, et al.
All Rights Reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
* Neither the name of Sony Pictures Imageworks nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <vector>
#include <string>
#include <cstdio>

#include <boost/algorithm/string.hpp>

#include "OpenImageIO/strutil.h"
#include "OpenImageIO/dassert.h"
#include "OpenImageIO/thread.h"
#include "OpenImageIO/filesystem.h"

#include "oslexec_pvt.h"
using namespace OSL;
using namespace OSL::pvt;



#ifdef OSL_NAMESPACE
namespace OSL_NAMESPACE {
#endif

namespace OSL {


ShadingSystem *
ShadingSystem::create (RendererServices *renderer,
                       TextureSystem *texturesystem,
                       ErrorHandler *err)
{
    // If client didn't supply an error handler, just use the default
    // one that echoes to the terminal.
    if (! err) {
        err = & ErrorHandler::default_handler ();
        ASSERT (err != NULL && "Can't create default ErrorHandler");
    }

    // Doesn't need a shared cache
    ShadingSystemImpl *ts = new ShadingSystemImpl (renderer, texturesystem, err);
#ifdef DEBUG
    err->info ("creating new ShadingSystem %p", (void *)ts);
#endif
    return ts;
}



void
ShadingSystem::destroy (ShadingSystem *x)
{
    delete (ShadingSystemImpl *) x;
}



ShadingSystem::ShadingSystem ()
{
}



ShadingSystem::~ShadingSystem ()
{
}



namespace Strings {

// Define static ustring symbols for very fast comparison
ustring camera ("camera"), common ("common");
ustring object ("object"), shader ("shader");
ustring rgb ("rgb"), RGB ("RGB");
ustring hsv ("hsv"), hsl ("hsl"), YIQ ("YIQ"), xyz ("xyz");
ustring null ("null"), default_("default");
ustring diffuse ("diffuse"), phong ("phong"), ward ("ward"), transparent ("transparent");
ustring microfacet_beckmann ("microfacet_beckmann"), microfacet_ggx ("microfacet_ggx");
ustring emission ("emission");
ustring P ("P"), I ("I"), N ("N"), Ng ("Ng");
ustring dPdu ("dPdu"), dPdv ("dPdv"), u ("u"), v ("v");
ustring time ("time"), dtime ("dtime"), dPdtime ("dPdtime");
ustring Ci ("Ci");
ustring width ("width"), swidth ("swidth"), twidth ("twidth");
ustring blur ("blur"), sblur ("sblur"), tblur ("tblur");
ustring wrap ("wrap"), swrap ("swrap"), twrap ("twrap");
ustring black ("black"), clamp ("clamp");
ustring periodic ("periodic"), mirror ("mirror");
ustring firstchannel ("firstchannel"), fill ("fill"), alpha ("alpha");
};



namespace pvt {   // OSL::pvt



ShadingSystemImpl::ShadingSystemImpl (RendererServices *renderer,
                                      TextureSystem *texturesystem,
                                      ErrorHandler *err)
    : m_renderer(renderer), m_texturesys(texturesystem), m_err(err),
      m_statslevel (0), m_debug (false), m_in_group (false),
      m_global_heap_total (0)
{
    m_stat_shaders_loaded = 0;
    m_stat_shaders_requested = 0;
    init_global_heap_offsets ();

    // If client didn't supply an error handler, just use the default
    // one that echoes to the terminal.
    if (! m_err) {
        m_err = & ErrorHandler::default_handler ();
    }

#if 0
    // If client didn't supply renderer services, create a default one
    if (! m_renderer) {
        m_renderer = NULL;
        ASSERT (m_renderer);
    }
#endif

    // If client didn't supply a texture system, create a new one
    if (! m_texturesys) {
        m_texturesys = TextureSystem::create (true /* shared */);
        ASSERT (m_texturesys);
        // Make some good guesses about default options
        m_texturesys->attribute ("automip",  1);
        m_texturesys->attribute ("autotile", 64);
    }
}



ShadingSystemImpl::~ShadingSystemImpl ()
{
    printstats ();
    // N.B. just let m_texsys go -- if we asked for one to be created,
    // we asked for a shared one.
}



bool
ShadingSystemImpl::attribute (const std::string &name, TypeDesc type,
                              const void *val)
{
    lock_guard guard (m_mutex);  // Thread safety
    if (name == "searchpath:shader" && type == TypeDesc::STRING) {
        m_searchpath = std::string (*(const char **)val);
        Filesystem::searchpath_split (m_searchpath, m_searchpath_dirs);
        return true;
    }
    if (name == "statistics:level" && type == TypeDesc::INT) {
        m_statslevel = *(const int *)val;
        return true;
    }
    if (name == "debug" && type == TypeDesc::INT) {
        m_debug = *(const int *)val;
        return true;
    }
    return false;
}



bool
ShadingSystemImpl::getattribute (const std::string &name, TypeDesc type,
                                 void *val)
{
    lock_guard guard (m_mutex);  // Thread safety
    if (name == "searchpath:shader" && type == TypeDesc::INT) {
        *(const char **)val = m_searchpath.c_str();
        return true;
    }
    if (name == "statistics:level" && type == TypeDesc::INT) {
        *(int *)val = m_statslevel;
        return true;
    }
    if (name == "debug" && type == TypeDesc::INT) {
        *(int *)val = m_debug;
        return true;
    }
    return false;
}



std::string
ShadingSystemImpl::geterror () const
{
    PerThreadInfo *threadinfo = get_perthread_info ();
    std::string e;
    std::swap (e, threadinfo->errormessage);  // will clear the stored one
    return e;
}



void
ShadingSystemImpl::error (const char *message, ...)
{
    std::string &err (get_perthread_info()->errormessage);
    if (err.size())
        err += '\n';
    va_list ap;
    va_start (ap, message);
    std::string msg = Strutil::vformat (message, ap);
    err += msg;
    m_err->error (msg);
    va_end (ap);
}



void
ShadingSystemImpl::info (const char *message, ...)
{
    va_list ap;
    va_start (ap, message);
    std::string msg = Strutil::vformat (message, ap);
    m_err->info (msg);
    va_end (ap);
}



std::string
ShadingSystemImpl::getstats (int level) const
{
    if (level <= 0)
        return "";
    std::ostringstream out;
    out << "OSL ShadingSystem statistics (" << (void*)this << ")\n";
    out << "  Shaders:\n";
    out << "    Requested: " << m_stat_shaders_requested << "\n";
    out << "    Loaded:    " << m_stat_shaders_loaded << "\n";
    out << "    Masters:   " << m_stat_shaders_loaded << "\n";
    out << "    Instances: " << m_stat_instances << "\n";
    out << "  Shading contexts: " << m_stat_contexts << "\n";
    // FIXME
    return out.str();
}



void
ShadingSystemImpl::printstats () const
{
    if (m_statslevel == 0)
        return;
    m_err->message (getstats (m_statslevel));
}



bool
ShadingSystemImpl::Parameter (const char *name, TypeDesc t, const void *val)
{
    m_pending_params.push_back (ParamRef (ustring(name), t, val));
    return true;
}



bool
ShadingSystemImpl::ShaderGroupBegin (void)
{
    if (m_in_group) {
        error ("Nested ShaderGroupBegin() calls");
        return false;
    }
    m_in_group = true;
    m_group_use = ShadUseUnknown;
    return true;
}



bool
ShadingSystemImpl::ShaderGroupEnd (void)
{
    if (! m_in_group) {
        error ("ShaderGroupEnd() was called without ShaderGroupBegin()");
        return false;
    }
    m_in_group = false;
    m_group_use = ShadUseUnknown;
    return true;
}



bool
ShadingSystemImpl::Shader (const char *shaderusage,
                           const char *shadername,
                           const char *layername)
{
    ShaderMaster::ref master = loadshader (shadername);
    if (! master) {
        error ("Could not find shader \"%s\"", shadername);
        return false;
    }

    ShaderUse use = shaderuse_from_name (shaderusage);
    if (use == ShadUseUnknown) {
        error ("Unknown shader usage \"%s\"", shaderusage);
        return false;
    }

    // Make sure we have a current attrib state
    if (! m_curattrib)
        m_curattrib.reset (new ShadingAttribState);

    // If somebody is already hanging onto the shader state, clone it before
    // we modify it.
    if (! m_curattrib.unique ()) {
        ShadingAttribStateRef newstate (new ShadingAttribState (*m_curattrib));
        m_curattrib = newstate;
    }

    ShaderInstanceRef instance (new ShaderInstance (master, layername));
    instance->parameters (m_pending_params);
    m_pending_params.clear ();

    ShaderGroup &shadergroup (m_curattrib->shadergroup (use));
    if (! m_in_group || m_group_use == ShadUseUnknown) {
        // A singleton, or the first in a group
        shadergroup.clear ();
    }
    if (m_in_group) {
        if (m_group_use == ShadUseUnknown) {  // First shader in group
            m_group_use = use;
        } else if (use != m_group_use) {
            error ("Shader usage \"%s\" does not match current group (%s)",
                   shaderusage, shaderusename (m_group_use));
            return false;
        }
    }

    shadergroup.append (instance);
    m_curattrib->changed_shaders ();

    // FIXME -- check for duplicate layer name within the group?

    return true;
}



bool
ShadingSystemImpl::ConnectShaders (const char *srclayer, const char *srcparam,
                                   const char *dstlayer, const char *dstparam)
{
    // Basic sanity checks -- make sure it's a legal time to call
    // ConnectShaders, and that the layer and parameter names are not empty.
    if (! m_in_group) {
        error ("ConnectShaders can only be called within ShaderGroupBegin/End");
        return false;
    }
    if (!srclayer || !srclayer[0] || !srcparam || !srcparam[0]) {
        error ("ConnectShaders: badly formed source layer/parameter");
        return false;
    }
    if (!dstlayer || !dstlayer[0] || !dstparam || !dstparam[0]) {
        error ("ConnectShaders: badly formed destination layer/parameter");
        return false;
    }

    // Decode the layers, finding the indices within our group and
    // pointers to the instances.  Error and return if they are not found,
    // or if it's not connecting an earlier src to a later dst.
    ShaderInstance *srcinst, *dstinst;
    int srcinstindex = find_named_layer_in_group (ustring(srclayer), srcinst);
    int dstinstindex = find_named_layer_in_group (ustring(dstlayer), dstinst);
    if (! srcinst) {
        error ("ConnectShaders: source layer \"%s\" not found", srclayer);
        return false;
    }
    if (! dstinst) {
        error ("ConnectShaders: destination layer \"%s\" not found", dstlayer);
        return false;
    }
    if (dstinstindex <= srcinstindex) {
        error ("ConnectShaders: destination layer must follow source layer\n");
        return false;
    }

    // Decode the parameter names, find their symbols in their
    // respective layers, and also decode requrest to attach specific
    // array elements or color/vector channels.
    ConnectedParam srccon = decode_connected_param(srcparam, srclayer, srcinst);
    ConnectedParam dstcon = decode_connected_param(dstparam, dstlayer, dstinst);
    if (! (srccon.valid() && dstcon.valid()))
        return false;

    if (! assignable (dstcon.type, srccon.type)) {
        error ("ConnectShaders: cannot connect a %s (%s) to a %s (%s)",
               srccon.type.c_str(), srcparam, dstcon.type.c_str(), dstparam);
        return false;
    }

    dstinst->add_connection (srcinstindex, srccon, dstcon);

    if (debug())
        m_err->message ("ConnectShaders %s %s -> %s %s\n",
                        srclayer, srcparam, dstlayer, dstparam);

    return true;
}



ShadingAttribStateRef
ShadingSystemImpl::state () const
{
    return m_curattrib;
}



void
ShadingSystemImpl::clear_state ()
{
    m_curattrib.reset (new ShadingAttribState);
}



ShadingContext *
ShadingSystemImpl::get_context ()
{
    PerThreadInfo *threadinfo = get_perthread_info ();
    if (threadinfo->context_pool.empty()) {
        return new ShadingContext (*this);
    } else {
        return threadinfo->pop_context ();
    }
}



void
ShadingSystemImpl::release_context (ShadingContext *sc)
{
    PerThreadInfo *threadinfo = get_perthread_info ();
    threadinfo->context_pool.push (sc);
}



ShadingContext *
ShadingSystemImpl::PerThreadInfo::pop_context ()
{
    ShadingContext *sc = context_pool.top ();
    context_pool.pop ();
    return sc;
}



ShadingSystemImpl::PerThreadInfo::~PerThreadInfo ()
{
    while (! context_pool.empty())
        delete pop_context ();
}



int
ShadingSystemImpl::find_named_layer_in_group (ustring layername,
                                              ShaderInstance * &inst)
{
    ShaderGroup &group (m_curattrib->shadergroup (m_group_use));
    for (int i = 0;  i < group.nlayers();  ++i) {
        if (group[i]->layername() == layername) {
            inst = group[i];
            return i;
        }
    }
    inst = NULL;
    return -1;
}



ConnectedParam
ShadingSystemImpl::decode_connected_param (const char *connectionname,
                                   const char *layername, ShaderInstance *inst)
{
    ConnectedParam c;  // initializes to "invalid"

    // Look for a bracket in the "parameter name"
    const char *bracket = strchr (connectionname, '[');
    // Grab just the part of the param name up to the bracket
    ustring param (connectionname, 0,
                   bracket ? size_t(bracket-connectionname) : ustring::npos);

    // Search for the param with that name, fail if not found
    c.param = inst->master()->findsymbol (param);
    if (c.param < 0) {
        error ("ConnectShaders: \"%s\" is not a parameter or global of layer \"%s\" (shader \"%s\")",
               param.c_str(), layername, inst->master()->shadername().c_str());
        return c;
    }

    Symbol *sym = inst->master()->symbol (c.param);
    ASSERT (sym);

    // Only params, output params, and globals are legal for connections
    if (! (sym->symtype() == SymTypeParam ||
           sym->symtype() == SymTypeOutputParam ||
           sym->symtype() == SymTypeGlobal)) {
        error ("ConnectShaders: \"%s\" is not a parameter or global of layer \"%s\" (shader \"%s\")",
               param.c_str(), layername, inst->master()->shadername().c_str());
        c.param = -1;  // mark as invalid
        return c;
    }

    c.type = sym->typespec();

    if (bracket && c.type.arraylength()) {
        // There was at least one set of brackets that appears to be
        // selecting an array element.
        c.arrayindex = atoi (bracket+1);
        if (c.arrayindex >= c.type.arraylength()) {
            error ("ConnectShaders: cannot request array element %s from a %s",
                   connectionname, c.type.c_str());
            c.arrayindex = c.type.arraylength() - 1;  // clamp it
        }
        c.type.make_array (0);              // chop to the element type
        c.offset += c.type.simpletype().size() * c.arrayindex;
        bracket = strchr (bracket+1, '[');  // skip to next bracket
    }

    if (bracket && ! c.type.is_closure() &&
            c.type.aggregate() != TypeDesc::SCALAR) {
        // There was at least one set of brackets that appears to be 
        // selecting a color/vector component.
        c.channel = atoi (bracket+1);
        if (c.channel >= (int)c.type.aggregate()) {
            error ("ConnectShaders: cannot request component %s from a %s",
                   connectionname, c.type.c_str());
            c.channel = (int)c.type.aggregate() - 1;  // clamp it
        }
        // chop to just the scalar part
        c.type = TypeSpec ((TypeDesc::BASETYPE)c.type.simpletype().basetype);
        c.offset += c.type.simpletype().size() * c.channel;
        bracket = strchr (bracket+1, '[');     // skip to next bracket
    }

    // Deal with left over brackets
    if (bracket) {
        // Still a leftover bracket, no idea what to do about that
        error ("ConnectShaders: don't know how to connect '%s' when \"%s\" is a \"%s\"",
               connectionname, param.c_str(), c.type.c_str());
        c.param = -1;  // mark as invalid
    }
    return c;
}



void
ShadingSystemImpl::init_global_heap_offsets ()
{
    lock_guard lock (m_mutex);
    if (m_global_heap_total > 0)
        return;   // Already initialized

    // FIXME -- these are all wrong (but luckily unused).  They don't 
    // properly consider npoints, let alone derivs or alignment.  Ugh.
    // Leave here for right now, anticipate future deletion or fixing.
    const int triple_size = sizeof (Vec3);
    m_global_heap_offsets[ustring("P")] = m_global_heap_total;
    m_global_heap_total += triple_size;
    m_global_heap_offsets[ustring("I")] = m_global_heap_total;
    m_global_heap_total += triple_size;
    m_global_heap_offsets[ustring("N")] = m_global_heap_total;
    m_global_heap_total += triple_size;
    m_global_heap_offsets[ustring("Ng")] = m_global_heap_total;
    m_global_heap_total += triple_size;
    m_global_heap_offsets[ustring("dPdu")] = m_global_heap_total;
    m_global_heap_total += triple_size;
    m_global_heap_offsets[ustring("dPdv")] = m_global_heap_total;
    m_global_heap_total += triple_size;

    m_global_heap_offsets[ustring("u")] = m_global_heap_total;
    m_global_heap_total += sizeof (float);
    m_global_heap_offsets[ustring("v")] = m_global_heap_total;
    m_global_heap_total += sizeof (float);
    m_global_heap_offsets[ustring("time")] = m_global_heap_total;
    m_global_heap_total += sizeof (float);
    m_global_heap_offsets[ustring("dtime")] = m_global_heap_total;
    m_global_heap_total += sizeof (float);
}



int
ShadingSystemImpl::global_heap_offset (ustring name)
{
    // FIXME -- these are all wrong.  I don't know what I was thinking
    // when I wrote this.  But luckily, it also doesn't seem to be used
    // now.
    ASSERT (0);
    std::map<ustring,int>::const_iterator f = m_global_heap_offsets.find (name);
    return f != m_global_heap_offsets.end() ? f->second : -1;
}


}; // namespace pvt
}; // namespace OSL

#ifdef OSL_NAMESPACE
}; // end namespace OSL_NAMESPACE
#endif
