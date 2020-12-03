#include "disc_tool.h"

#include "ibm_disc_format.h"
#include "disc.h"
#include "log.h"
#include "util.h"

#include <stdio.h>

#include <assert.h>
#include <string.h>

enum {
  k_max_sectors = 32,
};

struct disc_tool_struct {
  struct disc_struct* p_disc;
  int is_side_upper;
  uint32_t track;
  uint32_t track_length;
  uint32_t pos;
  struct disc_tool_sector sectors[k_max_sectors];
  uint32_t num_sectors;
};

struct disc_tool_struct*
disc_tool_create() {
  struct disc_tool_struct* p_tool =
      util_mallocz(sizeof(struct disc_tool_struct));

  return p_tool;
}

void
disc_tool_destroy(struct disc_tool_struct* p_tool) {
  util_free(p_tool);
}

uint32_t
disc_tool_get_byte_pos(struct disc_tool_struct* p_tool) {
  return (p_tool->pos / 32);
}

void
disc_tool_set_disc(struct disc_tool_struct* p_tool,
                   struct disc_struct* p_disc) {
  p_tool->p_disc = p_disc;
  disc_tool_set_track(p_tool, p_tool->track);
}

void
disc_tool_set_is_side_upper(struct disc_tool_struct* p_tool,
                            int is_side_upper) {
  p_tool->is_side_upper = is_side_upper;
  disc_tool_set_track(p_tool, p_tool->track);
}

void
disc_tool_set_track(struct disc_tool_struct* p_tool, uint32_t track) {
  p_tool->track = track;
  p_tool->num_sectors = 0;
  p_tool->track_length = disc_get_track_length(p_tool->p_disc,
                                               p_tool->is_side_upper,
                                               p_tool->track);
}

void
disc_tool_set_byte_pos(struct disc_tool_struct* p_tool, uint32_t pos) {
  if (pos >= p_tool->track_length) {
    pos = 0;
  }
  p_tool->pos = (pos * 32);
}

static uint32_t*
disc_tool_get_pulses(struct disc_tool_struct* p_tool) {
  struct disc_struct* p_disc = p_tool->p_disc;
  int is_side_upper = p_tool->is_side_upper;
  uint32_t track = p_tool->track;

  if (p_disc == NULL) {
    return NULL;
  }

  if (track >= k_ibm_disc_tracks_per_disc) {
    return NULL;
  }

  return disc_get_raw_pulses_buffer(p_disc, is_side_upper, track);
}

static uint32_t
disc_tool_read_pulses(struct disc_tool_struct* p_tool) {
  uint32_t pulses;
  uint32_t* p_pulses = disc_tool_get_pulses(p_tool);
  uint32_t pos = p_tool->pos;
  uint32_t pulses_pos = (pos / 32);
  uint32_t bit_pos = (pos % 32);
  uint32_t source_pulses = p_pulses[pulses_pos];

  if (p_pulses == NULL) {
    return 0;
  }

  pulses = (source_pulses << bit_pos);
  if (pulses_pos == p_tool->track_length) {
    pulses_pos = 0;
    p_tool->pos = bit_pos;
  } else {
    pulses_pos++;
    p_tool->pos += 32;
  }
  if (bit_pos > 0) {
    source_pulses = p_pulses[pulses_pos];
    pulses |= (source_pulses >> (32 - bit_pos));
  }

  return pulses;
}

void
disc_tool_read_fm_data(struct disc_tool_struct* p_tool,
                       uint8_t* p_clocks,
                       uint8_t* p_data,
                       uint32_t len) {
  uint32_t i;

  for (i = 0; i < len; ++i) {
    uint8_t clocks;
    uint8_t data;
    uint32_t pulses = disc_tool_read_pulses(p_tool);
    ibm_disc_format_2us_pulses_to_fm(&clocks, &data, pulses);
    if (p_clocks != NULL) {
      p_clocks[i] = clocks;
    }
    if (p_data != NULL) {
      p_data[i] = data;
    }
  }
}

static void
disc_tool_commit_write(struct disc_tool_struct* p_tool) {
  struct disc_struct* p_disc = p_tool->p_disc;
  if (p_disc == NULL) {
    return;
  }

  disc_dirty_and_flush(p_disc, p_tool->is_side_upper, p_tool->track);
}

void
disc_tool_write_fm_data(struct disc_tool_struct* p_tool,
                        uint8_t* p_clocks,
                        uint8_t* p_data,
                        uint32_t len) {
  uint32_t i;
  uint32_t pos = p_tool->pos;
  uint32_t pulses_pos = (pos / 32);
  uint32_t bit_pos = (pos % 32);
  uint32_t* p_pulses = disc_tool_get_pulses(p_tool);
  uint32_t track_length = p_tool->track_length;

  assert(bit_pos == 0);

  if (p_pulses == NULL) {
    return;
  }

  for (i = 0; i < len; ++i) {
    uint32_t pulses;
    uint8_t clocks = 0xFF;
    if (p_clocks != NULL) {
      clocks = p_clocks[i];
    }
    pulses = ibm_disc_format_fm_to_2us_pulses(clocks, p_data[i]);
    p_pulses[pulses_pos] = pulses;
    pulses_pos++;
    if (pulses_pos == track_length) {
      pulses_pos = 0;
      pos = 0;
    } else {
      pos += 32;
    }
  }

  p_tool->pos = pos;

  disc_tool_commit_write(p_tool);
}

void
disc_tool_fill_fm_data(struct disc_tool_struct* p_tool, uint8_t data) {
  uint32_t i;
  uint32_t pulses;
  uint32_t* p_pulses = disc_tool_get_pulses(p_tool);

  if (p_pulses == NULL) {
    return;
  }

  pulses = ibm_disc_format_fm_to_2us_pulses(0xFF, data);

  for (i = 0; i < k_ibm_disc_bytes_per_track; ++i) {
    p_pulses[i] = pulses;
  }

  p_tool->pos = 0;

  disc_tool_commit_write(p_tool);
}

static uint16_t
disc_tool_crc_add_run(uint16_t crc, uint8_t* p_data, uint32_t length) {
  uint32_t i;
  for (i = 0; i < length; ++i) {
    crc = ibm_disc_format_crc_add_byte(crc, p_data[i]);
  }
  return crc;
}

void
disc_tool_find_sectors(struct disc_tool_struct* p_tool, int is_mfm) {
  uint32_t i_pulses;
  uint32_t i_sectors;
  uint32_t bit_length;
  uint32_t shift_register;
  uint32_t num_shifts;
  uint32_t pulses;
  uint32_t num_sectors = 0;
  uint64_t mark_detector = 0;
  uint32_t* p_pulses = disc_tool_get_pulses(p_tool);
  struct disc_struct* p_disc = p_tool->p_disc;
  struct disc_tool_sector* p_sector = NULL;
  uint32_t track_length = p_tool->track_length;

  assert(!is_mfm);

  p_tool->num_sectors = 0;

  if (p_disc == NULL) {
    return;
  }

  /* Pass 1: walk the track and find header and data markers. */
  bit_length = (track_length * 32);
  shift_register = 0;
  num_shifts = 0;
  for (i_pulses = 0; i_pulses < bit_length; ++i_pulses) {
    uint8_t clocks;
    uint8_t data;

    if ((i_pulses & 31) == 0) {
      pulses = p_pulses[i_pulses / 32];
    }
    mark_detector <<= 1;
    shift_register <<= 1;
    num_shifts++;
    if (pulses & 0x80000000) {
      mark_detector |= 1;
      shift_register |= 1;
    }
    pulses <<= 1;

    if ((mark_detector & 0xFFFFFFFF00000000) != 0x8888888800000000) {
      continue;
    }

    ibm_disc_format_2us_pulses_to_fm(&clocks, &data, mark_detector);
    if (clocks != k_ibm_disc_mark_clock_pattern) {
      continue;
    }

    if (data == k_ibm_disc_id_mark_data_pattern) {
      if (num_sectors == k_max_sectors) {
        util_bail("too many sector headers");
      }
      p_sector = &p_tool->sectors[num_sectors];
      (void) memset(p_sector, '\0', sizeof(struct disc_tool_sector));
      num_sectors++;
      p_sector->bit_pos_header = i_pulses;
      shift_register = 0;
      num_shifts = 0;
    } else if ((data == k_ibm_disc_data_mark_data_pattern) ||
               (data == k_ibm_disc_deleted_data_mark_data_pattern)) {
      if ((p_sector == NULL) || (p_sector->bit_pos_data != 0)) {
        log_do_log(k_log_disc,
                   k_log_unusual,
                   "sector data without header on track %d",
                   p_tool->track);
      } else {
        assert(p_sector->bit_pos_header != 0);
        p_sector->bit_pos_data = i_pulses;
        if (data == k_ibm_disc_deleted_data_mark_data_pattern) {
          p_sector->is_deleted = 1;
        }
        shift_register = 0;
        num_shifts = 0;
      }
    }
  }

  /* Pass 2: walk the list of header markers, work out physical sector sizes,
   * and check CRCs.
   */
  for (i_sectors = 0; i_sectors < num_sectors; ++i_sectors) {
    uint8_t data;
    uint16_t crc;
    uint16_t disc_crc;
    struct disc_tool_sector* p_sector = &p_tool->sectors[i_sectors];
    uint8_t sector_data[2048 + 2];
    uint32_t sector_size;

    assert(p_sector->bit_pos_header != 0);
    p_tool->pos = p_sector->bit_pos_header;
    disc_tool_read_fm_data(p_tool, NULL, &p_sector->header_bytes[0], 6);
    crc = ibm_disc_format_crc_init(0);
    crc = ibm_disc_format_crc_add_byte(crc, k_ibm_disc_id_mark_data_pattern);
    crc = disc_tool_crc_add_run(crc, &p_sector->header_bytes[0], 4);
    disc_crc = (p_sector->header_bytes[4] << 8);
    disc_crc |= p_sector->header_bytes[5];
    if (crc != disc_crc) {
      p_sector->has_header_crc_error = 1;
    }

    if (p_sector->bit_pos_data == 0) {
      log_do_log(k_log_disc,
                 k_log_unusual,
                 "sector header without data on track %d",
                 p_tool->track);
      continue;
    }
    p_tool->pos = p_sector->bit_pos_data;
    crc = ibm_disc_format_crc_init(0);
    if (p_sector->is_deleted) {
      data = k_ibm_disc_deleted_data_mark_data_pattern;
    } else {
      data = k_ibm_disc_data_mark_data_pattern;
    }
    crc = ibm_disc_format_crc_add_byte(crc, data);
    sector_size = (128 << (p_sector->header_bytes[3] & 0x07));
    if (sector_size > 2048) {
      sector_size = 2048;
    }
    disc_tool_read_fm_data(p_tool, NULL, &sector_data[0], (sector_size + 2));
    crc = disc_tool_crc_add_run(crc, &sector_data[0], sector_size);
    disc_crc = (sector_data[sector_size] << 8);
    disc_crc |= sector_data[sector_size + 1];
    if (crc != disc_crc) {
      p_sector->has_data_crc_error = 1;
    }
  }

  p_tool->num_sectors = num_sectors;
}

struct disc_tool_sector*
disc_tool_get_sectors(struct disc_tool_struct* p_tool,
                      uint32_t* p_num_sectors) {
  *p_num_sectors = p_tool->num_sectors;
  return &p_tool->sectors[0];
}

void
disc_tool_log_summary(struct disc_struct* p_disc,
                      int log_crc_errors,
                      int log_protection) {
  uint32_t i_tracks;
  struct disc_tool_struct* p_tool = disc_tool_create();

  disc_tool_set_disc(p_tool, p_disc);
  for (i_tracks = 0; i_tracks < k_ibm_disc_tracks_per_disc; ++i_tracks) {
    uint32_t i_sectors;
    struct disc_tool_sector* p_sectors;
    uint32_t num_sectors;
    disc_tool_set_track(p_tool, i_tracks);
    disc_tool_find_sectors(p_tool, 0);
    p_sectors = disc_tool_get_sectors(p_tool, &num_sectors);
    for (i_sectors = 0; i_sectors < num_sectors; ++i_sectors) {
      if (log_crc_errors || log_protection) {
        if (p_sectors->has_header_crc_error) {
          log_do_log(k_log_disc,
                     k_log_warning,
                     "header CRC error track %d physical sector %d",
                     i_tracks,
                     i_sectors);
        }
        if (p_sectors->has_data_crc_error) {
          log_do_log(k_log_disc,
                     k_log_warning,
                     "data CRC error track %d physical sector %d",
                     i_tracks,
                     i_sectors);
        }
      }
      p_sectors++;
    }
  }

  disc_tool_destroy(p_tool);
}
