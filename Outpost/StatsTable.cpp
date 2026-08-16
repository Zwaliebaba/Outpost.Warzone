#include "pch.h"
#include "Frame.h"
#include "StatsTable.h"

BOOL StatsTable::Open(UBYTE* _buffer, UDWORD _size, const char* _name, StatsTable& _outTable)
{
  auto parsed = Neuron::Json::Parse(std::string_view(reinterpret_cast<char*>(_buffer), _size));
  if (!parsed.has_value())
  {
    Neuron::Fatal("{}: parse error at line {} column {}: {}",
                  _name, parsed.error().line, parsed.error().column, parsed.error().message);
    return FALSE;
  }
  if (!parsed->IsArray())
  {
    Neuron::Fatal("{}: a stats table must be a JSON array", _name);
    return FALSE;
  }

  _outTable.m_document = std::move(*parsed);
  _outTable.m_name = _name;

  return TRUE;
}

const Neuron::Json* StatsTable::Field(UDWORD _row, const char* _field, Neuron::Json::Kind _kind) const
{
  const Neuron::Json& row = m_document.Item(_row);
  if (!row.IsObject())
  {
    Neuron::Fatal("{} row {}: not an object", m_name, _row);
    return nullptr;
  }
  const Neuron::Json* field = row.Find(_field);
  if (field == nullptr)
  {
    Neuron::Fatal("{} row {}: missing field {}", m_name, _row, _field);
    return nullptr;
  }
  if (field->Type() != _kind)
  {
    Neuron::Fatal("{} row {}: field {} has the wrong kind", m_name, _row, _field);
    return nullptr;
  }
  return field;
}

BOOL StatsTable::Text(UDWORD _row, const char* _field, STRING* _out, UDWORD _outSize) const
{
  const Neuron::Json* field = Field(_row, _field, Neuron::Json::Kind::String);
  if (field == nullptr)
    return FALSE;
  if (field->AsString().size() >= _outSize)
  {
    Neuron::Fatal("{} row {}: field {} is too long", m_name, _row, _field);
    return FALSE;
  }
  strcpy(_out, field->AsString().c_str());
  return TRUE;
}

const char* StatsTable::TextRef(UDWORD _row, const char* _field) const
{
  const Neuron::Json* field = Field(_row, _field, Neuron::Json::Kind::String);
  return field != nullptr ? field->AsString().c_str() : nullptr;
}

BOOL StatsTable::Number(UDWORD _row, const char* _field, SDWORD* _out) const
{
  const Neuron::Json* field = Field(_row, _field, Neuron::Json::Kind::Number);
  if (field == nullptr)
    return FALSE;
  *_out = static_cast<SDWORD>(field->AsInt());
  return TRUE;
}

BOOL StatsTable::Number(UDWORD _row, const char* _field, UDWORD* _out) const
{
  const Neuron::Json* field = Field(_row, _field, Neuron::Json::Kind::Number);
  if (field == nullptr)
    return FALSE;
  *_out = static_cast<UDWORD>(field->AsInt());
  return TRUE;
}
