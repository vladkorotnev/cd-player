#pragma once
#include <strings.h>

char *rfc822_binary(void *src, unsigned long srcl, size_t *len);

struct MemoryBuffer : std::streambuf
{
  char* begin;
  char* end;

  MemoryBuffer(uint8_t* begin, uint8_t* end) :
    begin((char*)begin),
    end((char*)end)
  {
    this->setg((char*)begin, (char*)begin, (char*)end);
    this->setp((char*)begin, (char*)end);
  }

  virtual pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which = std::ios_base::in) override
  {
    pos_type ret;

    if ((which & std::ios_base::in) > 0)
    {
      if (dir == std::ios_base::cur)
      {
        gbump((int32_t)off);
      }
      else if (dir == std::ios_base::end)
      {
        setg(begin, end + off, end);
      }
      else if (dir == std::ios_base::beg)
      {
        setg(begin, begin + off, end);
      }

      ret = gptr() - eback();
    }
    
    if ((which & std::ios_base::out) > 0)
    {
      if (dir == std::ios_base::cur)
      {
        pbump((int32_t)off);
      }
      else if (dir == std::ios_base::end)
      {
        setp(end + off, end);
      }
      else if (dir == std::ios_base::beg)
      {
        setp(begin + off, end);
      }

      ret = pptr() - pbase();
    }

    return ret;

  }

  virtual pos_type seekpos(std::streampos pos, std::ios_base::openmode mode) override
  {
    return seekoff(pos - pos_type(off_type(0)), std::ios_base::beg, mode);
  }
};
