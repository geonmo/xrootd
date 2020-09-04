//------------------------------------------------------------------------------
// Copyright (c) 2011-2012 by European Organization for Nuclear Research (CERN)
// Author: Lukasz Janyst <ljanyst@cern.ch>
//------------------------------------------------------------------------------
// XRootD is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// XRootD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with XRootD.  If not, see <http://www.gnu.org/licenses/>.
//------------------------------------------------------------------------------

#ifndef __XRD_CL_MESSAGE_HH__
#define __XRD_CL_MESSAGE_HH__

#include "XrdCl/XrdClBuffer.hh"

namespace XrdCl
{
  //----------------------------------------------------------------------------
  //! The message representation used throughout the system
  //----------------------------------------------------------------------------
  class Message: public Buffer
  {
    public:
      //------------------------------------------------------------------------
      //! Constructor
      //------------------------------------------------------------------------
      Message( uint32_t size = 0 ):
        Buffer( size ), pIsMarshalled( false ), pSessionId(0)
      {
        if( size )
          Zero();
      }

      //------------------------------------------------------------------------
      //! Move constructor
      //------------------------------------------------------------------------
      Message( Message && msg ) : Buffer( std::move( msg ) )
      {
        pIsMarshalled = msg.pIsMarshalled;
        pSessionId    = msg.pSessionId;
        pDescription  = msg.pDescription;
      }

      //------------------------------------------------------------------------
      //! Move assigment operator
      //------------------------------------------------------------------------
      Message& operator=( Message && msg )
      {
        if( this == &msg ) return *this;

        pIsMarshalled = msg.pIsMarshalled;
        pSessionId    = msg.pSessionId;
        pDescription  = msg.pDescription;

        Steal( std::move( msg ) );

        return *this;
      }

      //------------------------------------------------------------------------
      //! Destructor
      //------------------------------------------------------------------------
      virtual ~Message() {}

      //------------------------------------------------------------------------
      //! Check if the message is marshalled
      //------------------------------------------------------------------------
      bool IsMarshalled() const
      {
        return pIsMarshalled;
      }

      //------------------------------------------------------------------------
      //! Set the marshalling status
      //------------------------------------------------------------------------
      void SetIsMarshalled( bool isMarshalled )
      {
        pIsMarshalled = isMarshalled;
      }

      //------------------------------------------------------------------------
      //! Set the description of the message
      //------------------------------------------------------------------------
      void SetDescription( const std::string &description )
      {
        pDescription = description;
      }

      //------------------------------------------------------------------------
      //! Get the description of the message
      //------------------------------------------------------------------------
      const std::string &GetDescription() const
      {
        return pDescription;
      }

      //------------------------------------------------------------------------
      //! Set the session ID which this message is meant for
      //------------------------------------------------------------------------
      void SetSessionId( uint64_t sessionId )
      {
        pSessionId = sessionId;
      }

      //------------------------------------------------------------------------
      //! Get the session ID the message is meant for
      //------------------------------------------------------------------------
      uint64_t GetSessionId() const
      {
        return pSessionId;
      }

    private:
      bool         pIsMarshalled;
      uint64_t     pSessionId;
      std::string  pDescription;
  };
}

#endif // __XRD_CL_MESSAGE_HH__
