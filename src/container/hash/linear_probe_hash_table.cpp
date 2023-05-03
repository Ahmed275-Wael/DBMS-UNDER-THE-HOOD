//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// linear_probe_hash_table.cpp
//
// Identification: src/container/hash/linear_probe_hash_table.cpp
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <iostream>

#include <string>

#include <utility>

#include <vector>

#include "common/exception.h"

#include "common/logger.h"

#include "common/rid.h"

#include "container/hash/linear_probe_hash_table.h"

namespace bustub {

  template < typename KeyType, typename ValueType, typename KeyComparator >
    HASH_TABLE_TYPE::LinearProbeHashTable(const std::string & name,
      BufferPoolManager * buffer_pool_manager,
      const KeyComparator & comparator,
        size_t num_buckets,
        HashFunction < KeyType > hash_fn): buffer_pool_manager_(buffer_pool_manager), comparator_(comparator), hash_fn_(std::move(hash_fn)) {

      Page * newPage = buffer_pool_manager_ -> NewPage( & header_page_id_);
      auto headerPage = reinterpret_cast < HashTableHeaderPage * > (newPage -> GetData());
      headerPage -> SetPageId(header_page_id_);

      headerPage -> SetSize(num_buckets);

      auto blockNum = num_buckets / BLOCK_ARRAY_SIZE;

      page_id_t tmpPageId;
      for (unsigned i = 0; i < blockNum; i++) {

        buffer_pool_manager -> NewPage( & tmpPageId);

        headerPage -> AddBlockPageId(tmpPageId);

        buffer_pool_manager -> UnpinPage(tmpPageId, true);
      }

      buffer_pool_manager -> UnpinPage(header_page_id_, true);
    }
  template < typename KeyType, typename ValueType, typename KeyComparator >
    bool HASH_TABLE_TYPE::GetValue(Transaction * transaction,
      const KeyType & key, std::vector < ValueType > * result) {
      table_latch_.RLock();
      Page * hPage = buffer_pool_manager_ -> FetchPage(header_page_id_);
      HashTableHeaderPage * headerPage = reinterpret_cast < HashTableHeaderPage * > (hPage -> GetData());

      auto mmHash = hash_fn_.GetHash(key);

      auto targetBlockIndex = mmHash % headerPage -> NumBlocks();
      auto blockPageId = headerPage -> GetBlockPageId(targetBlockIndex);

      Page * page = buffer_pool_manager_ -> FetchPage(blockPageId);
      HASH_TABLE_BLOCK_TYPE * blockPage = reinterpret_cast < HASH_TABLE_BLOCK_TYPE * > (page -> GetData());
      auto res = false;
      bool foundEmptySlot = false;
      size_t visitedBlocks = 1;
      while (!foundEmptySlot && visitedBlocks < headerPage -> NumBlocks()) {
        for (long unsigned int i = 0; i < BLOCK_ARRAY_SIZE; i++) {
          if (blockPage -> IsValid(i) && comparator_(blockPage -> KeyAt(i), key) == 0) {
           result->push_back(blockPage->ValueAt(i));
           res = true;
            break;

          } else if (!blockPage -> IsOccupied(i)) {
            foundEmptySlot = true;
          }
        }
        targetBlockIndex = (targetBlockIndex + 1) % headerPage -> NumBlocks();
        buffer_pool_manager_ -> UnpinPage(blockPageId, true);
        blockPageId = headerPage -> GetBlockPageId(targetBlockIndex);
        page = buffer_pool_manager_ -> FetchPage(blockPageId);
        blockPage = reinterpret_cast < HASH_TABLE_BLOCK_TYPE * > (page -> GetData());
        visitedBlocks++;
      }
      //unpin all page
      buffer_pool_manager_ -> UnpinPage(blockPageId, true);
      buffer_pool_manager_ -> UnpinPage(header_page_id_, true);

      return res;
    }

  template < typename KeyType, typename ValueType, typename KeyComparator >
    bool HASH_TABLE_TYPE::Insert(Transaction * transaction,
      const KeyType & key,
        const ValueType & value) {
      table_latch_.RLock();
      Page * hPage = buffer_pool_manager_ -> FetchPage(header_page_id_);
      if (hPage == nullptr) return false;
      HashTableHeaderPage * headerPage = reinterpret_cast < HashTableHeaderPage * > (hPage -> GetData());

      auto mmHash = hash_fn_.GetHash(key);
      auto targetBlockIndex = mmHash % headerPage -> NumBlocks();
      auto blockPageId = headerPage -> GetBlockPageId(targetBlockIndex);
      Page * page = buffer_pool_manager_ -> FetchPage(blockPageId);
      HASH_TABLE_BLOCK_TYPE * blockPage = reinterpret_cast < HASH_TABLE_BLOCK_TYPE * > (page -> GetData());
      auto res = false;
      bool found = false;

      size_t visitedBlocks = 1;
      //Check if the block is empty and no duplicate (key,value)
      while (blockPage -> IsFull() && visitedBlocks <= headerPage -> NumBlocks()) {
        if (blockPage -> isKeyValueExist(key, value, comparator_)) {
          found = true;
          break;
        } else {
          targetBlockIndex = (targetBlockIndex + 1) % headerPage -> NumBlocks();
          buffer_pool_manager_ -> UnpinPage(blockPageId, true);
          blockPageId = headerPage -> GetBlockPageId(targetBlockIndex);
          page = buffer_pool_manager_ -> FetchPage(blockPageId);
          blockPage = reinterpret_cast < HASH_TABLE_BLOCK_TYPE * > (page -> GetData());
        }
        visitedBlocks++;
      }

      if (!found && visitedBlocks > headerPage -> NumBlocks()) {
        //Resizing
            Resize(headerPage->GetSize());
            return this->Insert(transaction, key, value);
      } else if (!found) {
         page->WLatch();
        for (long unsigned i = 0; i < BLOCK_ARRAY_SIZE; i++) {
          if (!blockPage -> IsOccupied(i)) {
            blockPage -> Insert(i, key, value);
            res = true;
            break;
          }
        }
         page->WUnLatch();
      }
      //unpin all page
      buffer_pool_manager_ -> UnpinPage(blockPageId, true);
      buffer_pool_manager_ -> UnpinPage(header_page_id_, true);

      return res;
    }

  template < typename KeyType, typename ValueType, typename KeyComparator >
    bool HASH_TABLE_TYPE::Remove(Transaction * transaction,
      const KeyType & key,
        const ValueType & value) {
       table_latch_.RLock();     
      Page * hPage = buffer_pool_manager_ -> FetchPage(header_page_id_);
      if (hPage == nullptr) return false;
      HashTableHeaderPage * headerPage = reinterpret_cast < HashTableHeaderPage * > (hPage -> GetData());

      auto mmHash = hash_fn_.GetHash(key);

      auto targetBlockIndex = mmHash % headerPage -> NumBlocks();
      auto blockPageId = headerPage -> GetBlockPageId(targetBlockIndex);

      Page * page = buffer_pool_manager_ -> FetchPage(blockPageId);
      if (page == nullptr) return false;
      HASH_TABLE_BLOCK_TYPE * blockPage = reinterpret_cast < HASH_TABLE_BLOCK_TYPE * > (page -> GetData());
      auto res = false;
      bool foundEmptySlot = false;
      size_t visitedBlocks = 1;
      while (!foundEmptySlot && visitedBlocks < headerPage -> NumBlocks()) {
        for (long unsigned int i = 0; i < BLOCK_ARRAY_SIZE; i++) {
           page->WLatch();
          if (blockPage -> IsValid(i) && comparator_(blockPage -> KeyAt(i), key) == 0 &&
            blockPage -> ValueAt(i) == value) {

            blockPage -> Remove(i);
            res = true;
            break;

          } else if (!blockPage -> IsOccupied(i)) {
            foundEmptySlot = true;
          }
           page->WUnLatch();

        }
        targetBlockIndex = (targetBlockIndex + 1) % headerPage -> NumBlocks();
        buffer_pool_manager_ -> UnpinPage(blockPageId, true);
        blockPageId = headerPage -> GetBlockPageId(targetBlockIndex);
        page = buffer_pool_manager_ -> FetchPage(blockPageId);
        blockPage = reinterpret_cast < HASH_TABLE_BLOCK_TYPE * > (page -> GetData());
        visitedBlocks++;
      }
      //unpin all page
      buffer_pool_manager_ -> UnpinPage(blockPageId, true);
      buffer_pool_manager_ -> UnpinPage(header_page_id_, true);

      return res;
    }

  /***************************
   * RESIZE
   ***************************/
  template < typename KeyType, typename ValueType, typename KeyComparator >
    void HASH_TABLE_TYPE::Resize(size_t initial_size) {
        this->table_latch_.WLock();

          auto newSize = initial_size * 2;
          Page * hPage = buffer_pool_manager_ -> FetchPage(header_page_id_);
          if (hPage == nullptr) return;
          HashTableHeaderPage * headerPage = reinterpret_cast < HashTableHeaderPage * > (hPage -> GetData());
          if (headerPage->GetSize() >= newSize) return;

          //Save Old Data
          auto oldBlocks = std::vector<HashTableBlockPage<KeyType, ValueType, KeyComparator> *>();
          auto oldPageIds = std::vector<page_id_t>();
          for (size_t i = 0; i < headerPage->NumBlocks(); i++) {
            page_id_t currentPageId = headerPage->GetBlockPageId(i);
            oldBlocks.push_back( reinterpret_cast<HashTableBlockPage<KeyType, ValueType, KeyComparator> *>(this->buffer_pool_manager_->FetchPage(currentPageId)->GetData() ));
            oldPageIds.push_back(currentPageId);
            this->buffer_pool_manager_->UnpinPage(currentPageId, false);
          }
        //Create New Blocks From Scratch (Need to be refactored DUPLICATE CODE WITH CTR)
          headerPage -> SetSize(newSize);
          headerPage->ResetNextIndex();
          auto blockNum = newSize / BLOCK_ARRAY_SIZE;
          page_id_t tmpPageId;
          for (unsigned i = 0; i < blockNum; i++) {
           buffer_pool_manager_ -> NewPage( & tmpPageId);
           headerPage -> AddBlockPageId(tmpPageId);
           buffer_pool_manager_ -> UnpinPage(tmpPageId, true);
           }
          //ReHash The Existing Data
          for (size_t blockIndex = 0; blockIndex < oldBlocks.size(); blockIndex++) {
           const auto &block = oldBlocks[blockIndex];
           for (size_t pairIndex = 0; pairIndex < BLOCK_ARRAY_SIZE; pairIndex++) {
           this->Insert(nullptr, block->KeyAt(pairIndex), block->ValueAt(pairIndex));
            }
           this->buffer_pool_manager_->DeletePage(oldPageIds[blockIndex]);
          }

           buffer_pool_manager_ -> UnpinPage(header_page_id_, true);
            this->table_latch_.WUnlock();

    }

  template < typename KeyType, typename ValueType, typename KeyComparator >
    size_t HASH_TABLE_TYPE::GetSize() {

      Page * hPage = buffer_pool_manager_ -> FetchPage(header_page_id_);
      HashTableHeaderPage * headerPage = reinterpret_cast < HashTableHeaderPage * > (hPage -> GetData());
      // auto size = headerPage->count;
      int size = headerPage -> GetSize();
      buffer_pool_manager_ -> UnpinPage(header_page_id_, false);
      return size;

    }

  template
  class LinearProbeHashTable < int, int, IntComparator > ;

  template
  class LinearProbeHashTable < GenericKey < 4 > , RID, GenericComparator < 4 >> ;

  template
  class LinearProbeHashTable < GenericKey < 8 > , RID, GenericComparator < 8 >> ;

  template
  class LinearProbeHashTable < GenericKey < 16 > , RID, GenericComparator < 16 >> ;

  template
  class LinearProbeHashTable < GenericKey < 32 > , RID, GenericComparator < 32 >> ;

  template
  class LinearProbeHashTable < GenericKey < 64 > , RID, GenericComparator < 64 >> ;

} // namespace bustub