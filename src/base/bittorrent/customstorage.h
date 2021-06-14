/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2020  Vladimir Golovnev <glassez@yandex.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 */

#pragma once

#include <libtorrent/aux_/vector.hpp>
#include <libtorrent/fwd.hpp>
#include <libtorrent/version.hpp>

#include <QString>

#include <libtorrent/storage.hpp>
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/file_pool.hpp"
#include "libtorrent/file_storage.hpp"


lt::storage_interface *customStorageConstructor(const lt::storage_params &params, lt::file_pool &pool);

class CustomStorage final : public lt::storage_interface
{
public:
    explicit CustomStorage(lt::storage_params const& params, lt::file_pool& pool);
    ~CustomStorage();
    void initialize(lt::storage_error&) override;
    bool has_any_file(lt::storage_error&) override { return false; }
    void set_file_priority(lt::aux::vector<lt::download_priority_t, lt::file_index_t>&
        , lt::storage_error&) override {}
    void rename_file(lt::file_index_t, std::string const&, lt::storage_error&) override
    { assert(false); }
    lt::status_t move_storage(std::string const&
        , lt::move_flags_t, lt::storage_error&) override { return lt::status_t::no_error; }
    bool verify_resume_data(lt::add_torrent_params const&
        , lt::aux::vector<std::string, lt::file_index_t> const&
        , lt::storage_error&) override
    { return false; }
    void release_files(lt::storage_error&) override {
        // make sure we don't have the files open
        lt::file_handle defer_delete = std::move(m_fh);
		m_pool.release(storage_index());
    }
    void delete_files(lt::remove_flags_t, lt::storage_error&) override {
        lt::file_handle defer_delete = std::move(m_fh);
        m_pool.release(storage_index());
    }

    int readv(lt::span<lt::iovec_t const> bufs, lt::piece_index_t piece
        , int offset, lt::open_mode_t, lt::storage_error&) override;

    int writev(lt::span<lt::iovec_t const> bufs
        , lt::piece_index_t const piece, int offset, lt::open_mode_t, lt::storage_error&) override;

private:
    std::string m_save_path;
    lt::file_pool& m_pool;
    lt::file_storage m_onefile;
    lt::file_handle m_fh;
};
