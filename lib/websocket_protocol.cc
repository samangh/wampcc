
#include "XXX/websocket_protocol.h"

#include "XXX/utils.h"
#include "XXX/io_handle.h"

#include <iostream>

#include <string.h>

#include <openssl/sha.h>
#include <arpa/inet.h>


namespace XXX
{



websocket_protocol::websocket_protocol(io_handle* h, t_msg_cb msg_cb, connection_mode m)
  : protocol(h, msg_cb, m),
    m_get_found(false),
    m_request_crlf_found(false)
{
}


bool websocket_protocol::is_http_get(const char* s, size_t len)
{
  if (len < HEADER_SIZE) return false;
  return ( strncmp(s, "GET", 3)==0 && (s[3] == ' ' || s[3]=='\t'));
}


const char cb64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const void* dataVoid, size_t length) {
    std::string output;
    auto data = reinterpret_cast<const uint8_t*>(dataVoid);
    for (auto i = 0u; i < length; i += 3) {
        auto bytesLeft = length - i;
        auto b0 = data[i];
        auto b1 = bytesLeft > 1 ? data[i + 1] : 0;
        auto b2 = bytesLeft > 2 ? data[i + 2] : 0;
        output.push_back(cb64[b0 >> 2]);
        output.push_back(cb64[((b0 & 0x03) << 4) | ((b1 & 0xf0) >> 4)]);
        output.push_back((bytesLeft > 1 ? cb64[((b1 & 0x0f) << 2) | ((b2 & 0xc0) >> 6)] : '='));
        output.push_back((bytesLeft > 2 ? cb64[b2 & 0x3f] : '='));
    }
    return output;
}



inline std::string make_accept_key(const std::string& challenge)
{
  auto full_key = challenge + websocket_protocol::MAGIC;
  unsigned char obuf[20];

  SHA1((const unsigned char*)full_key.c_str(), full_key.size(), obuf);


  // SHA1 hasher;
  // hasher.Input(fullString.c_str(), fullString.size());
  // unsigned hash[5];
  // hasher.Result(hash);
  // for (int i = 0; i < 5; ++i) {
  //   hash[i] = htonl(hash[i]);
  // }
  return base64Encode(obuf, 20);
}



static bool string_list_contains(const std::string & source,
                                 const std::string & match)
{

  for (auto & i : tokenize(source.c_str(), ',', true))
  {
    std::string trimmed = trim(i);
    if (case_insensitive_same(trimmed, match)) return true;
  }
  return false;
}



// Two byte conversion union
union uint16_converter
{
    uint16_t i;
    uint8_t  m[2];
};

// Four byte conversion union
union uint32_converter
{
    uint32_t i;
    uint8_t m[4];
};

// Eight byte conversion union
union uint64_converter
{
    uint64_t i;
    uint8_t  m[8];
};

struct frame_builder
{
  // Minimum length of a WebSocket frame header.
  static unsigned int const BASIC_HEADER_LENGTH = 2;

  // Maximum length of a WebSocket header (2+8+4)
  static unsigned int const MAX_HEADER_LENGTH = 14;

  static uint8_t const FRAME_OPCODE = 0x0F;
  static uint8_t const FRAME_FIN    = 0x80;
  static uint8_t const FRAME_MASKED = 0x80;


  /** Construct  */
  frame_builder(int opcode, bool is_fin, size_t payload_len)
  {
    unsigned char is_fin_bit    = is_fin?FRAME_FIN:0;

    image[0] = is_fin_bit | (FRAME_OPCODE & opcode);

    if (payload_len < 126)
    {
      image[1] = (unsigned char)(payload_len);
      header_len = 2;
    }
    else if (payload_len < 65536)
    {
      uint16_converter temp;
      temp.i = htons(payload_len & 0xFFFF);
      image[1] = (unsigned char)126;
      image[2] = temp.m[0];
      image[3] = temp.m[1];
      header_len = 4;
    }
    else
    {
      uint64_converter temp;
      temp.i = __bswap_64(payload_len);
      image[1] = (unsigned char)127;
      for (int i = 0; i<8; ++i) image[2+i]=temp.m[i];
      header_len = 10;
    }
  }

  frame_builder(int opcode, bool is_fin, size_t payload_len, std::array<char,4> mask)
    :  frame_builder(opcode, is_fin, payload_len)
  {
    image[1] |= FRAME_MASKED;
    image[header_len+0] = mask[0];
    image[header_len+1] = mask[1];
    image[header_len+2] = mask[2];
    image[header_len+3] = mask[3];
    header_len += 4;
  }

  char * data() { return image; }
  size_t size() const { return header_len; }
private:

  // Storage for frame being created.
  char   image[MAX_HEADER_LENGTH];

  // Actual frame size, since frame size depends on payload size and masking
  size_t header_len;
};



void websocket_protocol::encode(const jalson::json_array& ja)
{
  std::string msg ( jalson::encode( ja ) );

  // TODO: now I have data to send


  // prepare frame

  frame_builder fb(OPCODE_TEXT, true, msg.size());

  std::pair<const char*, size_t> bufs[2];

  bufs[0].first  = (char*)fb.data();
  bufs[0].second = fb.size();

  bufs[1].first  = (const char*)msg.c_str();;
  bufs[1].second = msg.size();

  m_iohandle->write_bufs(bufs, 2, false);
}



void websocket_protocol::io_on_read(char* src, size_t len)
{
  while (len) /* IO thread */
  {
    size_t consume_len = m_buf.consume(src, len);
    src += consume_len;
    len -= consume_len;

    auto rd = m_buf.read_ptr();
    while (rd.avail())
    {
      auto consumed = parse_http_handshake(rd.ptr(), rd.avail());
      rd.advance(consumed);

      if (state == eServerHandshakeWait)
      {
        if (m_request_crlf_found)
        {
          if ( m_http_headers.count("Connection")  &&
               m_http_headers.count("Upgrade") &&
               string_list_contains(m_http_headers["Connection"], "Upgrade") &&
               m_http_headers.count("Sec-WebSocket-Key") &&
               m_http_headers.count("Sec-WebSocket-Version") )
          {
            std::cout << "found expected" << "\n";
            auto websocket_version = std::stoi(m_http_headers["Sec-WebSocket-Version"]);

            // TODO: hande version 0 ?

            if (websocket_version == 13) // RFC6455
            {
              std::ostringstream os;
              os << "HTTP/1.1 101 Switching Protocols" << "\r\n";
              os << "Upgrade: websocket" << "\r\n";
              os << "Connection: Upgrade" << "\r\n";
              os << "Sec-WebSocket-Accept: " << make_accept_key(m_http_headers["Sec-WebSocket-Key"]) << "\r\n";
              os << "\r\n";

              std::string msg = os.str();
              std::pair<const char*, size_t> buf;
              buf.first  = msg.c_str();
              buf.second = msg.size();
              std::cout << "websocket header sent back\n";
              m_iohandle->write_bufs(&buf, 1, false);
              state = eServerHandshareSent;
            }
            else
            {
              throw std::runtime_error("invalid websocket version");
            }
          }
        }
      }
      else
      {
        if (rd.avail() < 2) break;

        bool       fin_bit = rd[0] & 0x80;
        int         opcode = rd[0] & 0x0F;
        bool      mask_bit = rd[1] & 0x80;
        size_t payload_len = rd[1] & 0x7F;
        size_t   frame_len = 2 + (mask_bit? 4:0);
        int       mask_pos = 2;
        int    payload_pos;

        if (payload_len == 126)
        {
          if (rd.avail() < 4) break;
          frame_len   += 2;
          mask_pos    += 2;
          uint16_t raw_length;
          memcpy(&raw_length, &rd[2], 2);
          payload_len = ntohs(raw_length);
        }
        else if (payload_len == 127)
        {
          if (rd.avail() < 10) break;
          frame_len   += 8;
          mask_pos    += 8;

          uint64_t raw_length;
          memcpy(&raw_length, &rd[2], 8);
          payload_len = __bswap_64(raw_length);

        }
        frame_len += payload_len;
        payload_pos = mask_pos + (mask_bit? 4:0);

        if (rd.avail() < frame_len) break;

        for (size_t i = 0; i < (mask_bit?payload_len:0); ++i)
          rd[payload_pos+i] ^= rd[mask_pos + (i%4)];

        std::cout << "fin=" << fin_bit << ", opcode=" << opcode << ", "
                  << "framelen=" << frame_len << ", ";
        if (mask_bit) std::cout << "mask=" << to_hex(&rd[mask_pos], 4) << ", ";
        std::cout << "payloadlen=" << payload_len << ", ";

        std::string payload(&rd[payload_pos], payload_len);
        std::cout << "payload=" << payload << "\n";

        if (!fin_bit)
          throw std::runtime_error("websocket continuations not yet supported");


        switch (opcode)
        {
          case 0x00: /* cont. */
          case 0x01: /* text  */
          case 0x02: /* bin.  */
          default: break;
        }

        if (fin_bit && opcode == OPCODE_TEXT)
          decode_json(rd.ptr()+payload_pos, payload_len);

        rd.advance(frame_len);
      }

    }

    m_buf.discard_read( rd ); /* shift unused bytes to front of buffer */
  } // while(len)
}

void websocket_protocol::initiate(t_initiate_cb)
{
  throw std::runtime_error("websocket_protocol::initiate not implemented");
}



size_t websocket_protocol::parse_http_handshake(char* const src, size_t const len)
{
  char * first = src;
  size_t consumed = 0;
  for (char * ptr = first; ptr < (src+len-1); ++ptr)
  {
    if (ptr[0] == '\r' && ptr[1] == '\n')
    {
      ptr[0] = 0;
      ptr[1] = 0;
      consumed = ptr - src + 2;

      first = skip_whitespace(first);
      if (m_get_found == false && is_http_get(first, strlen(first)))
      {
        std::cout << "found GET: " << first << "\n";
        m_get_found = true;
      }
      else if (first == ptr)
      {
        std::cout << "m_request_crlf_found" << "\n";
        m_request_crlf_found = true;
      }
      else
      {
        char * second = strchr(first,':');
        if (second != nullptr)
        {
          *second = '\0';
          second = skip_whitespace(second+1);
          std::cout << "found: " << first << " -- " << second << "\n";
          m_http_headers.emplace(first, second);
        }
        else
        {
          std::cout << "unexpected line: " << first << "\n";
        }
      }
      first = ptr + 2;
      ptr++;
    }
  }

  return consumed;
}


void websocket_protocol::ev_on_timer()
{
  frame_builder fb(OPCODE_PING, true, 0);

  std::pair<const char*, size_t> bufs[1];

  bufs[0].first  = (char*)fb.data();
  bufs[0].second = fb.size();

  m_iohandle->write_bufs(bufs, 1, false);
}



}
