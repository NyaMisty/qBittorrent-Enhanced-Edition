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

#include "customstorage.h"

#include <libtorrent/download_priority.hpp>

#include <QDir>

#include <QString>
#include "base/global.h"
#include "base/utils/fs.h"
#include "base/logger.h"
#include "common.h"
#include <libtorrent/aux_/path.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/operations.hpp>
#include <libtorrent/aux_/storage_utils.hpp>

// delete the files belonging to the torrent from disk.
		// including the part-file, if there is one
static constexpr lt::remove_flags_t remove_flags_delete_files = lt::bit_t{static_cast<int>(0)};

// delete just the part-file associated with this torrent
static constexpr lt::remove_flags_t remove_flags_delete_partfile = lt::bit_t{static_cast<int>(1)};


namespace robber {
	template<typename Tag>
	struct result {
	/* export it ... */
	typedef typename Tag::type type;
	static type ptr;
	};

	template<typename Tag>
	typename result<Tag>::type result<Tag>::ptr;

	template<typename Tag, typename Tag::type p>
	struct rob : result<Tag> {
	/* fill it ... */
	struct filler {
		filler() { result<Tag>::ptr = p; }
	};
	static filler filler_obj;
	};

	template<typename Tag, typename Tag::type p>
	typename rob<Tag, p>::filler rob<Tag, p>::filler_obj;

	struct MtorrentGetter { typedef std::shared_ptr<void> lt::storage_interface::*type; };
	template class rob<MtorrentGetter, &CustomStorage::m_torrent>;

	std::shared_ptr<void> &get_mtorrent(CustomStorage &s) {
		return s.*result<MtorrentGetter>::ptr;
	}

}

CustomStorage::CustomStorage(lt::storage_params const& params, lt::file_pool& pool) : lt::storage_interface(params.files), m_pool(pool) {
	m_save_path = lt::complete(params.path);
	LogMsg(QString("Created custom storage, savepath %1.").arg(m_save_path.c_str()), Log::INFO);
	LogMsg(QString("First file location: %1").arg(params.files.file_path(lt::file_index_t{0}).c_str()), Log::INFO);
	LogMsg(QString("Torrent name: %1").arg(params.files.name().c_str()), Log::INFO);
}

CustomStorage::~CustomStorage()
{
	// this may be called from a different
	// thread than the disk thread
	m_pool.release(storage_index());
}

void CustomStorage::_persist_handle(lt::storage_error& ec) {
	if (m_fh) {
		return;
	}
	lt::error_code e;
	m_fh = m_pool.open_file(storage_index(), m_save_path, lt::file_index_t{0}, m_onefile, lt::open_mode::read_write | lt::open_mode::sparse, e);
	if (e) {
		ec.ec = e;
		ec.file(lt::file_index_t{0});
		ec.operation = lt::operation_t::file;
		return;
	}
	LogMsg(QString("%1: Successfully opened file handle!").arg(m_onefile.name().c_str()), Log::INFO);
}

void CustomStorage::initialize(lt::storage_error& ec) {
	LogMsg(QString("Initialized custom storage, piecelen %1, piecenum %2, totalsize %3.")
		.arg(std::to_string(files().piece_length()).c_str())
		.arg(std::to_string(files().num_pieces()).c_str())
		.arg(std::to_string(files().total_size()).c_str())
		, Log::INFO);
	m_onefile.set_piece_length(files().piece_length());
	m_onefile.set_num_pieces(files().num_pieces());
	m_onefile.set_name(files().name());
	m_onefile.add_file(files().name(), files().total_size());
	delete_files(remove_flags_delete_partfile, ec);
}

void CustomStorage::_release_files(bool force, lt::storage_error&) {
	LogMsg(QString("_release_file called! forced = %1").arg(std::to_string(force).c_str()), Log::INFO);
	bool needRelease = false;
	if (force) {
		needRelease = true;
	} else {
		void *_m_torrent = robber::get_mtorrent(*this).get();
		if (_m_torrent) {
			lt::torrent *__torrent = (lt::torrent *)_m_torrent;
			if (__torrent->is_finished()) {
				LogMsg(QString("%1: release because finished!").arg(m_onefile.name().c_str()), Log::INFO);
				needRelease = true;
			}
		} else {
		}
	}
	if (needRelease) {
		LogMsg(QString("%1: released!").arg(m_onefile.name().c_str()), Log::INFO);
		lt::file_handle defer_delete = std::move(m_fh);
		m_pool.release(storage_index());
	}
}

void CustomStorage::release_files(lt::storage_error &ec) {
	// make sure we don't have the files open
	_release_files(false, ec);
}
void CustomStorage::delete_files(lt::remove_flags_t flags, lt::storage_error& ec) {
	_release_files(true, ec);
	lt::aux::delete_files(m_onefile, m_save_path, m_onefile.name(), remove_flags_delete_partfile, ec);
}

int CustomStorage::readv(lt::span<lt::iovec_t const> bufs, lt::piece_index_t piece
        , int offset, lt::open_mode_t, lt::storage_error&)
{
	// auto const i = m_file_data.find(piece);
	// if (i == m_file_data.end()) return 0;
	// if (int(i->second.size()) <= offset) return 0;
	// lt::iovec_t data{ i->second.data() + offset, int(i->second.size() - offset) };
	// int ret = 0;
	// for (lt::iovec_t const& b : bufs) {
	// 	int const to_copy = std::min(int(b.size()), int(data.size()));
	// 	memcpy(b.data(), data.data(), to_copy);
	// 	data = data.subspan(to_copy);
	// 	ret += to_copy;
	// 	if (data.empty()) break;
	// }
	// return ret;
	return lt::bufs_size(bufs);
}

int CustomStorage::writev(lt::span<lt::iovec_t const> bufs
	, lt::piece_index_t const piece, int offset, lt::open_mode_t flags, lt::storage_error& ec)
{
	_persist_handle(ec);
	// auto& data = m_file_data[piece];
	// int ret = 0;
	// for (auto& b : bufs) {
	// 	if (int(data.size()) < offset + b.size()) data.resize(offset + b.size());
	// 	std::memcpy(data.data() + offset, b.data(), b.size());
	// 	offset += int(b.size());
	// 	ret += int(b.size());
	// }
	// return ret;
	lt::file_handle fh;
	{
		lt::error_code e;
		fh = m_pool.open_file(storage_index(), m_save_path, lt::file_index_t{0}, m_onefile, lt::open_mode::read_write | lt::open_mode::sparse | (flags & lt::open_mode::coalesce_buffers), e);
		if (e) {
			ec.ec = e;
			ec.file(lt::file_index_t{0});
			ec.operation = lt::operation_t::file_open;
			return -1;
		}
	}
	{
		lt::error_code e;
		std::int64_t pieceoff = std::int64_t(files().piece_length()) * int(piece);
		std::int64_t targetoff = pieceoff + offset;
		// LogMsg(QString("Writing piece %1 offset %2, finaloff %3")
		// 		.arg(std::to_string(int(piece)).c_str())
		// 		.arg(std::to_string(offset).c_str())
		// 		.arg(std::to_string(targetoff).c_str())
		// 		, Log::INFO);

		int const ret = int(fh->writev(targetoff, bufs, e, flags));

		// set this unconditionally in case the upper layer would like to treat
		// short reads as errors
		ec.operation = lt::operation_t::file_write;

		// we either get an error or 0 or more bytes read
		TORRENT_ASSERT(e || ret >= 0);
		TORRENT_ASSERT(ret <= bufs_size(vec));

		if (e)
		{
			ec.ec = e;
			ec.file(lt::file_index_t{0});
			return -1;
		}

		return ret;
	}
}

lt::storage_interface *customStorageConstructor(const lt::storage_params &params, lt::file_pool &pool)
{
    //return new CustomStorage {params, pool};
    return new CustomStorage {params, pool};
}