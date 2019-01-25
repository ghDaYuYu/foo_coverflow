#include "stdafx.h"
#include "config.h"

#include "DbAlbumCollection.h"

#include "AppInstance.h"
#include "ImgTexture.h"
#include "RenderThread.h"

#include "DbReloadWorker.h"

collection_read_lock::collection_read_lock(AppInstance& appInstance) :
	boost::shared_lock<DbAlbumCollection>(*(appInstance.albumCollection)){};

collection_read_lock::collection_read_lock(AppInstance* appInstance) :
	collection_read_lock(*appInstance){};

DbAlbumCollection::DbAlbumCollection(AppInstance* instance):
		appInstance(instance), targetPos(albums.get<1>().end()){

	static_api_ptr_t<titleformat_compiler> compiler;
	compiler->compile_safe_ex(cfgAlbumTitleScript, cfgAlbumTitle);
}

void DbAlbumCollection::onCollectionReload(DbReloadWorker& worker){
	ASSERT_EXCLUSIVE(this);
	
	// Synchronize TargetPos
	CollectionPos newTargetPos;
	auto &newSortedIndex = worker.albums.get<1>();
	if (albums.size() == 0){
		if (worker.albums.size() > (t_size)sessionSelectedCover){
			newTargetPos = newSortedIndex.nth(sessionSelectedCover);
		} else {
			newTargetPos = newSortedIndex.begin();
		}
	} else {
		newTargetPos = newSortedIndex.begin();
		CollectionPos oldTargetPos = *targetPos;
		pfc::string8_fast_aggressive albumKey;
		for (t_size i = 0; i < oldTargetPos->tracks.get_size(); i++){
			oldTargetPos->tracks[i]->format_title(0, albumKey, worker.albumMapper, 0);
			if (worker.albums.count(albumKey.get_ptr())){
				newTargetPos = worker.albums.project<1>(worker.albums.find(albumKey.get_ptr()));
				break;
			}
		}
	}
	albums = std::move(worker.albums);
	albumMapper = std::move(worker.albumMapper);

	setTargetPos(newTargetPos);
}

bool DbAlbumCollection::getArtForTrack(
		const metadb_handle_ptr &track,
		album_art_data::ptr &out){
	static_api_ptr_t<album_art_manager_v2> aam;
	pfc::list_t<GUID> guids;
	guids.add_item(album_art_ids::cover_front);
	metadb_handle_list tracks;
	tracks.add_item(track);
	abort_callback_impl abortCallback;
	album_art_extractor_instance_v2::ptr extractor = aam->open(tracks, guids, abortCallback);
	return extractor->query(album_art_ids::cover_front, out, abortCallback);
}

bool DbAlbumCollection::getTracks(CollectionPos pos, metadb_handle_list& out){
	out = pos->tracks;
	out.sort_by_format(cfgInnerSort, nullptr);
	return true;
}

bool DbAlbumCollection::getAlbumForTrack(const metadb_handle_ptr& track, CollectionPos& out){
	auto &sortedIndex = albums.get<1>();
	pfc::string8_fast_aggressive albumKey;
	if (!albumMapper.is_valid())
		return false;
	track->format_title(0, albumKey, albumMapper, 0);
	if (albums.count(albumKey.get_ptr())){
		auto groupAlbum = albums.find(albumKey.get_ptr());
		auto sortAlbum = albums.project<1>(groupAlbum);
		int idx = sortedIndex.rank(sortAlbum);
		out = sortAlbum;
		return true; 
	} else {
		return false;
	}
}

void DbAlbumCollection::getTitle(CollectionPos pos, pfc::string_base& out){
	auto &sortedIndex = albums.get<1>();
	pos->tracks[0]->format_title(0, out, cfgAlbumTitleScript, 0);
}


shared_ptr<ImgTexture> DbAlbumCollection::getImgTexture(const std::string& albumName){
	IF_DEBUG(profiler(DbAlbumCollection__getImgTexture));
	auto& albumIndex = albums.get<0>();
	auto& album = albumIndex.find(albumName);
	album_art_data::ptr art;
	if (getArtForTrack(album->tracks[0], art))
		return make_shared<ImgTexture>(art);
	// No Image found
	return nullptr;
}

struct CompIUtf8Partial
{
	bool operator()(pfc::string8 a, const char * b)const{
		return stricmp_utf8_partial(a, b) < 0;
	}
	bool operator()(const char * a, pfc::string8 b)const{
		return stricmp_utf8_partial(b, a) > 0;
	}
};

bool DbAlbumCollection::performFayt(const char * title, CollectionPos& out){
	auto &faytIndex = albums.get<2>();
	auto range = faytIndex.equal_range(title, CompIUtf8Partial());

	if (range.first == range.second){
		return false;
	} else {
		auto &sortIndex = albums.get<1>();
		t_size outIdx = ~0;
		out = sortIndex.begin();

		// find the item with the lowest index (this is important to select the leftmost album)
		for (auto it = range.first; it != range.second; ++it){
			CollectionPos thisPos = albums.project<1>(it);
			t_size thisIdx = sortIndex.rank(thisPos);
			if (thisIdx < outIdx){
				outIdx = thisIdx;
				out = thisPos;
			}
		}
		return true;
	}
}


CollectionPos DbAlbumCollection::begin() const{
	return albums.get<1>().begin();
}

CollectionPos DbAlbumCollection::end() const{
	return albums.get<1>().end();
}

t_size DbAlbumCollection::rank(CollectionPos p) {
	return albums.get<1>().rank(p);
}




void DbAlbumCollection::setTargetPos(CollectionPos newTarget) {
	ASSERT_SHARED(this);
	*targetPos = newTarget;
	sessionSelectedCover = this->rank(newTarget);
	appInstance->renderer->send(make_shared<RTTargetChangedMessage>());
}

void DbAlbumCollection::moveTargetBy(int n)
{
	auto target = targetPos.synchronize();
	movePosBy(*target, n);
	sessionSelectedCover = this->rank(*target);
	appInstance->renderer->send(make_shared<RTTargetChangedMessage>());
}