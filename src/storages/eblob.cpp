#include "cocaine/storages/eblobs.hpp"

using namespace cocaine::helpers;
using namespace cocaine::storage::backends;

namespace fs = boost::filesystem;

bool eblob_collector_t::callback(const zbr::eblob_disk_control* dco, const void* data, int) {
    if(dco->flags & BLOB_DISK_CTL_REMOVE) {
        return true;
    }

    std::string value(
        static_cast<const char*>(data),
        dco->data_size);

    Json::Value object;

    if(!m_reader.parse(value, object)) {
        // TODO: Have to find out the storage name somehow
        throw std::runtime_error("corrupted data");
    } 
   
    // TODO: Have to find out the key somehow 
    m_root[auto_uuid_t().get()] = object;
    
    return true;
}

bool eblob_purger_t::callback(const zbr::eblob_disk_control* dco, const void* data, int) {
    m_keys.push_back(dco->key);
    return true;
}

void eblob_purger_t::complete(uint64_t, uint64_t) {
    for(key_list_t::const_iterator it = m_keys.begin(); it != m_keys.end(); ++it) {
        // XXX: Is there a possibility for an exception here?
        m_eblob.remove_all(*it);
    }
}

eblob_storage_t::eblob_storage_t():
    m_storage_path(config_t::get().storage.location),
    m_logger(NULL, EBLOB_LOG_NOTICE)
{
    if(!fs::exists(m_storage_path)) {
        try {
            fs::create_directories(m_storage_path);
        } catch(const std::runtime_error& e) {
            throw std::runtime_error("cannot create " + m_storage_path.string());
        }
    } else if(fs::exists(m_storage_path) && !fs::is_directory(m_storage_path)) {
        throw std::runtime_error(m_storage_path.string() + " is not a directory");
    }
}

eblob_storage_t::~eblob_storage_t() {
    m_eblobs.clear();
}

void eblob_storage_t::put(const std::string& store, const std::string& key, const Json::Value& value) {
    eblob_map_t::iterator it(m_eblobs.find(store));
    
    if(it == m_eblobs.end()) {
        zbr::eblob_config cfg;

        memset(&cfg, 0, sizeof(cfg));
        cfg.file = const_cast<char*>((m_storage_path / store).string().c_str());
        cfg.iterate_threads = 1;
        cfg.sync = 5;
        cfg.log = m_logger.log();

        try {
            boost::tie(it, boost::tuples::ignore) = m_eblobs.insert(store, new zbr::eblob(&cfg));
        } catch(const std::runtime_error& e) {
            // TODO: Have to do something more sophisticated here
            throw;
        }
    }
        
    Json::FastWriter writer;
    std::string object(writer.write(value));

    try {
        it->second->write_hashed(key, object, 0);
    } catch(const std::runtime_error& e) {
        // TODO: Have to do something more sophisticated here
        throw;
    }
}

bool eblob_storage_t::exists(const std::string& store, const std::string& key) {
    eblob_map_t::iterator it(m_eblobs.find(store));
    
    if(it != m_eblobs.end()) {
        std::string object;

        try {
            object = it->second->read_hashed(key, 0, 0);
        } catch(const std::runtime_error& e) {
            // TODO: Have to do something more sophisticated here
            throw;
        }

        return !object.empty();
    }

    return false;
}

Json::Value eblob_storage_t::get(const std::string& store, const std::string& key) {
    Json::Value result(Json::objectValue);
    eblob_map_t::iterator it(m_eblobs.find(store));
    
    if(it != m_eblobs.end()) {
        Json::Reader reader(Json::Features::strictMode());
        std::string object;

        try {
            object = it->second->read_hashed(key, 0, 0);
        } catch(const std::runtime_error& e) {
            // TODO: Have to do something more sophisticated here
            throw;
        }

        if(!object.empty() && !reader.parse(object, result)) {
            throw std::runtime_error("corrupted data in '" + store + "'");
        }
    }

    return result;
}

Json::Value eblob_storage_t::all(const std::string& store) const {
    eblob_collector_t collector;
    
    try {
        zbr::eblob_iterator iterator((m_storage_path / store).string(), true);
        iterator.iterate(collector, 1);
    } catch(...) {
        // XXX: Does it only mean that the blob is empty?
        return Json::Value(Json::objectValue);
    }

    return collector.seal();
}

void eblob_storage_t::remove(const std::string& store, const std::string& key) {
    eblob_map_t::iterator it(m_eblobs.find(store));
    
    if(it != m_eblobs.end()) {
        try {
            it->second->remove_hashed(key);
        } catch(const std::runtime_error& e) {
            throw std::runtime_error("failed to remove from '" + store + "'");
        }
    }
}

void eblob_storage_t::purge(const std::string& store) {
    eblob_map_t::iterator it(m_eblobs.find(store));

    if(it != m_eblobs.end()) {
        eblob_purger_t purger(it->second);
        
        try {
            zbr::eblob_iterator iterator((m_storage_path / store).string(), true);
            iterator.iterate(purger, 1);
        } catch(...) {
            // FIXME: I have no idea what thit means
        }
    }
}