/*
 * StatsTable.h
 *
 * Row access for the JSON stats tables. A table file is a JSON array of
 * objects, one per record, in the order the old comma-separated file kept
 * its lines - array order still derives the REF_* reference numbers, so it
 * is load-bearing. A missing or wrong-kind field is a fatal data error,
 * which is the point: the sscanf loaders filled short rows with zeroes
 * silently.
 */
#ifndef _statstable_h
#define _statstable_h

#include <string>

#include "Json.h"

class StatsTable
{
public:
  /* Parse a table buffer; fatal (with the table name) unless the document
     is a JSON array. */
  [[nodiscard]] static BOOL Open(UBYTE* _buffer, UDWORD _size, const char* _name, StatsTable& _outTable);

  [[nodiscard]] UDWORD Rows() const { return static_cast<UDWORD>(m_document.Size()); }

  /* Field access on row _row. Text copies into a caller buffer; TextRef
     returns a pointer that lives as long as this StatsTable. All report
     table/row/field and fail on a missing field or a wrong kind. */
  [[nodiscard]] BOOL Text(UDWORD _row, const char* _field, STRING* _out, UDWORD _outSize) const;
  [[nodiscard]] const char* TextRef(UDWORD _row, const char* _field) const; // nullptr on error
  [[nodiscard]] BOOL Number(UDWORD _row, const char* _field, SDWORD* _out) const;
  [[nodiscard]] BOOL Number(UDWORD _row, const char* _field, UDWORD* _out) const;

private:
  [[nodiscard]] const Neuron::Json* Field(UDWORD _row, const char* _field, Neuron::Json::Kind _kind) const;

  Neuron::Json m_document;
  std::string m_name;
};

#endif
