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
#include "osoreader.h"




#ifdef OSL_NAMESPACE
namespace OSL_NAMESPACE {
#endif
namespace OSL {
namespace pvt {   // OSL::pvt


/// Custom subclass of OSOReader that provide callbacks that set all the
/// right fields in the ShaderMaster.
class OSOReaderToMaster : public OSOReader
{
public:
    OSOReaderToMaster (ShadingSystemImpl &shadingsys)
        : OSOReader (&shadingsys.errhandler()), m_shadingsys (shadingsys),
          m_master (new ShaderMaster (shadingsys)), m_reading_instruction(false)
      { }
    virtual ~OSOReaderToMaster () { }
    virtual bool parse (const std::string &filename);
    virtual void version (const char *specid, float version) { }
    virtual void shader (const char *shadertype, const char *name);
    virtual void symbol (SymType symtype, TypeSpec typespec, const char *name);
    virtual void symdefault (int def);
    virtual void symdefault (float def);
    virtual void symdefault (const char *def);
    virtual void hint (const char *hintstring);
    virtual void codemarker (const char *name);
    virtual void instruction (int label, const char *opcode);
    virtual void instruction_arg (const char *name);
    virtual void instruction_jump (int target);
    virtual void instruction_end ();

    ShaderMaster::ref master () const { return m_master; }

private:
    ShadingSystemImpl &m_shadingsys;
    ShaderMaster::ref m_master;
    size_t m_firstarg;
    size_t m_nargs;
    bool m_reading_instruction;
    ustring m_sourcefile;
    int m_sourceline;
};



bool
OSOReaderToMaster::parse (const std::string &filename)
{
    m_master->m_osofilename = filename;
    return OSOReader::parse (filename);
}



void
OSOReaderToMaster::shader (const char *shadertype, const char *name)
{
    m_master->m_shadername = name; //ustring(name);
    m_master->m_shadertype = shadertype_from_name (shadertype);
}



void
OSOReaderToMaster::symbol (SymType symtype, TypeSpec typespec, const char *name)
{
    Symbol sym (ustring(name), typespec, symtype);
    if (sym.symtype() == SymTypeParam || sym.symtype() == SymTypeOutputParam) {
        if (typespec.simpletype().basetype == TypeDesc::FLOAT)
            sym.dataoffset ((int) m_master->m_fdefaults.size());
        else if (typespec.simpletype().basetype == TypeDesc::INT)
            sym.dataoffset ((int) m_master->m_idefaults.size());
        else if (typespec.simpletype().basetype == TypeDesc::STRING)
            sym.dataoffset ((int) m_master->m_sdefaults.size());
        else {
            ASSERT (0 && "unexpected type");
        }
    }
    if (sym.symtype() == SymTypeConst) {
        if (typespec.simpletype().basetype == TypeDesc::FLOAT)
            sym.dataoffset ((int) m_master->m_fconsts.size());
        else if (typespec.simpletype().basetype == TypeDesc::INT)
            sym.dataoffset ((int) m_master->m_iconsts.size());
        else if (typespec.simpletype().basetype == TypeDesc::STRING)
            sym.dataoffset ((int) m_master->m_sconsts.size());
        else {
            ASSERT (0 && "unexpected type");
        }
    }
#if 0
    // FIXME -- global_heap_offset is quite broken.  But also not necessary.
    // We made need to fix this later.
    if (sym.symtype() == SymTypeGlobal) {
        sym.dataoffset (m_shadingsys.global_heap_offset (sym.name()));
    }
#endif
    m_master->m_symbols.push_back (sym);
}



void
OSOReaderToMaster::symdefault (int def)
{
    ASSERT (m_master->m_symbols.size() && "symdefault but no sym");
    Symbol &sym (m_master->m_symbols.back());
    if (sym.symtype() == SymTypeParam || sym.symtype() == SymTypeOutputParam) {
        if (sym.typespec().simpletype().basetype == TypeDesc::FLOAT)
            m_master->m_fdefaults.push_back ((float)def);
        else if (sym.typespec().simpletype().basetype == TypeDesc::INT)
            m_master->m_idefaults.push_back (def);
        else {
            ASSERT (0 && "unexpected type");
        }
    } else if (sym.symtype() == SymTypeConst) {
        if (sym.typespec().simpletype().basetype == TypeDesc::FLOAT)
            m_master->m_fconsts.push_back ((float)def);
        else if (sym.typespec().simpletype().basetype == TypeDesc::INT)
            m_master->m_iconsts.push_back (def);
        else {
            ASSERT (0 && "unexpected type");
        }
    }
}



void
OSOReaderToMaster::symdefault (float def)
{
    ASSERT (m_master->m_symbols.size() && "symdefault but no sym");
    Symbol &sym (m_master->m_symbols.back());
    if (sym.symtype() == SymTypeParam || sym.symtype() == SymTypeOutputParam) {
        if (sym.typespec().simpletype().basetype == TypeDesc::FLOAT)
            m_master->m_fdefaults.push_back (def);
        else {
            ASSERT (0 && "unexpected type");
        }
    } else if (sym.symtype() == SymTypeConst) {
        if (sym.typespec().simpletype().basetype == TypeDesc::FLOAT)
            m_master->m_fconsts.push_back (def);
        else {
            ASSERT (0 && "unexpected type");
        }
    }
}



void
OSOReaderToMaster::symdefault (const char *def)
{
    ASSERT (m_master->m_symbols.size() && "symdefault but no sym");
    Symbol &sym (m_master->m_symbols.back());
    if (sym.symtype() == SymTypeParam || sym.symtype() == SymTypeOutputParam) {
        if (sym.typespec().simpletype().basetype == TypeDesc::STRING)
            m_master->m_sdefaults.push_back (ustring(def));
        else {
            ASSERT (0 && "unexpected type");
        }
    } else if (sym.symtype() == SymTypeConst) {
        if (sym.typespec().simpletype().basetype == TypeDesc::STRING)
            m_master->m_sconsts.push_back (ustring(def));
        else {
            ASSERT (0 && "unexpected type");
        }
    }
}



// If the string 'source' begins with 'pattern', erase the pattern from
// the start of source and return true.  Otherwise, do not alter source
// and return false.
inline bool
extract_prefix (std::string &source, const std::string &pattern)
{
    if (boost::starts_with (source, pattern)) {
        source.erase (0, pattern.length());
        return true;
    }
    return false;
}



// Return the prefix of source that doesn't contain any characters in
// 'stop', erase that prefix from source (up to and including the stop
// character.  Also, the returned string is trimmed of leading and trailing
// spaces if 'do_trim' is true.
static std::string
readuntil (std::string &source, const std::string &stop, bool do_trim=false)
{
    size_t e = source.find_first_of (stop);
    std::string r (source, 0, e);
    source.erase (0, e == source.npos ? e : e+1);
    if (do_trim)
        boost::trim (r);
    return r;
}



void
OSOReaderToMaster::hint (const char *hintstring)
{
    std::string h (hintstring);
    if (extract_prefix (h, "%filename{\"")) {
        m_sourcefile = readuntil (h, "\"");
        return;
    }
    if (extract_prefix (h, "%line{")) {
        m_sourceline = atoi (h.c_str());
        return;
    }
}



void
OSOReaderToMaster::codemarker (const char *name)
{
    m_sourcefile.clear();
}



void
OSOReaderToMaster::instruction (int label, const char *opcode)
{
    Opcode op (ustring(opcode), ustring(""));
    m_master->m_ops.push_back (op);
    m_firstarg = m_master->m_args.size();
    m_nargs = 0;
    m_reading_instruction = true;
}



void
OSOReaderToMaster::instruction_arg (const char *name)
{
    ustring argname (name);
    for (size_t i = 0;  i < m_master->m_symbols.size();  ++i) {
        if (m_master->m_symbols[i].name() == argname) {
            m_master->m_args.push_back (i);
            ++m_nargs;
            return;
        }
    }
    // ERROR! -- FIXME
//    m_master->m_args.push_back (0);  // FIXME
    m_shadingsys.error ("Parsing shader: unknown arg %s", name);
}



void
OSOReaderToMaster::instruction_jump (int target)
{
    m_master->m_ops.back().add_jump (target);
}



void
OSOReaderToMaster::instruction_end ()
{
    m_master->m_ops.back().set_args (m_firstarg, m_nargs);
    m_master->m_ops.back().source (m_sourcefile, m_sourceline);
    m_reading_instruction = false;
}



ShaderMaster::ref
ShadingSystemImpl::loadshader (const char *cname)
{
    if (! cname || ! cname[0]) {
        error ("Attempt to load shader with empty name \"\".");
        return NULL;
    }
    ++m_stat_shaders_requested;
    ustring name (cname);
    lock_guard guard (m_mutex);  // Thread safety
    ShaderNameMap::const_iterator found = m_shader_masters.find (name);
    if (found != m_shader_masters.end()) {
        if (debug())
            info ("Found %s in shader_masters", name.c_str());
        // Already loaded this shader, return its reference
        return (*found).second;
    }

    // Not found in the map
    OSOReaderToMaster oso (*this);
    std::string filename = Filesystem::searchpath_find (name.string() + ".oso",
                                                        m_searchpath_dirs);
    if (filename.empty ()) {
        // FIXME -- error
        error ("No .oso file could be found for shader \"%s\"", name.c_str());
        return NULL;
    }
    bool ok = oso.parse (filename);
    ShaderMaster::ref r = ok ? oso.master() : NULL;
    m_shader_masters[name] = r;
    if (ok) {
        ++m_stat_shaders_loaded;
        if (debug())
            info ("Added %s to shader_masters", filename.c_str());
    } else {
        error ("Unable to read \"%s\"", filename.c_str());
    }
    // FIXME -- catch errors

    if (r) {
        r->resolve_syms ();
        r->resolve_ops ();
    }

    if (r && m_debug) {
        std::string s = r->print ();
        if (s.length())
            info ("%s", s.c_str());
    }

    return r;
}



}; // namespace pvt
}; // namespace OSL

#ifdef OSL_NAMESPACE
}; // end namespace OSL_NAMESPACE
#endif
