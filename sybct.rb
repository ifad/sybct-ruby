#  Copyright (C) 1999-2001  Tetsuhumi Takaishi

require "sybct.o"
include SybConstant


# SybConnection.open
#   (SybContext server user &REST passwd app-name hostname)
#    server is nil , use $DSQUERY 
class SybConnection
  def SybConnection.open (*args)
    conn = nil
    raise("illegal args count") if( args.length < 3 ) 
    ctx = args[0]
    srv = args[1]
    user = args[2]
    psw = "" ; appname = nil; hname = nil;
    psw = args[3] if(args.length >= 4 )
    appname = args[4] if(args.length >= 5 )
    hname = args[5] if(args.length >= 6 )
    serveraddr = args[6] if(args.length >= 7 )
    begin
      conn = SybConnection.new( ctx )
      if( hname != nil ) then
	raise("NG: setprop(hostname)") unless( conn.setprop(CS_HOSTNAME,hname ))
      end
      if( serveraddr != nil ) then  # DBI patch by Charls 
	raise("NG: setprop(serveraddr)") unless( conn.setprop(CS_SERVERADDR,serveraddr ))
      end
      # Open a Server connection.
      conn.connect( ctx, srv, user, psw, appname )
    rescue
      conn.delete if( conn.kind_of?( SybConnection ) )
      conn = nil  # No use ( for GC or callback ??? )
      raise	# Regenerates exception
    end
    return conn
  end
end

class SybCommand
  
  #attr "bind_numeric2fixnum",true
  attr_accessor "bind_numeric2fixnum"
  attr "column_types"

  # does fetchloop() fetch of CS_ROW_FAIL row ?
  # false --> fetchloop() returns nil if CS_ROW_FAIL comes
  ## @fetch_rowfail = false
  def fetch_rowfail( val )
    @fetch_rowfail = val
  end

  # rowproc
  #  fetchloop(n,strip,clm, proc { |cmd, status, column, row| ... } )
  #    Return value for rowproc 
  #      true	Adds *row* to fetchloop's return value
  #      nil	Does't add row to fetchloop's return value
  #		(When large number of rows are retrieved,this is effective 
  #                to save memory)
  #      false	Cancels fetch. ( fetchloop returns the value until the time)
  def fetchloop ( maxrows=0, strip=true, max_bind_column=nil, rowproc=nil )
    $stderr.print("FetchLoop in\n") if( $DEBUG) 
    rcnt = 0
    rowlist = []
    columns = self.bind_columns( max_bind_column)
    unless ( columns) then
      self.cancel( CS_CANCEL_CURRENT)
      return false
    end

    # fetch loop
    while( true )
      (fetch_status, row) = self.fetch(strip)
      case fetch_status
      when CS_END_DATA
	break
      when CS_SUCCEED, CS_ROW_FAIL
	if( rowproc != nil ) then
	  ## There is specification of block.
	  ret_proc = rowproc.call(self, fetch_status, columns, row)
	  if( ret_proc == false ) then
	    # Cancel fetch
	    self.cancel( CS_CANCEL_CURRENT)
	    fetch_status = CS_END_DATA
	    break
	  elsif ( ret_proc != nil ) then
	    # touch rowlist
	    rowlist.push( row )
	  end
	else
	  ## There is no specification of block. 
	  # row binds NIL if recovable ERROR
	  if ( (! @fetch_rowfail) && (fetch_status == CS_ROW_FAIL) ) then
	    row = nil 
	  end
	  rowlist.push( row )
	end
	rcnt += 1
      else
	$stderr.print("break UNK#{fetch_status} in fetchloop\n") if( $DEBUG)
	break
      end  # End of fetch_status CASE

      # Check fetch size exceed?
      if( (maxrows > 0) && (maxrows <= rcnt) ) then
	self.cancel(CS_CANCEL_CURRENT)  # Too late!
	### self.cancel(CS_CANCEL_ALL)
	fetch_status = CS_END_DATA
	break
      end

    end  # End of fetch loop

    # check last ct_fetch status
    if( fetch_status == CS_END_DATA ) then
      return [ columns, rowlist]
    else
      $stderr.print("CANCELALL begin  in fetchloop\n") if( $DEBUG) # RDEBUG
      # !!! if core dump in async mode, then comment out to self.delete !!!
      # self.cancel( CS_CANCEL_CURRENT) --> OLD version
      # self.cancel( CS_CANCEL_ALL) #
      self.delete 
      $stderr.print("CANCELALL end in fetchloop\n") if( $DEBUG) # RDEBUG
      return false
    end
  end	# End of fetchloop method

  # Retrieves cursor status
  #  returns CS_CURSTAT_NONE, CS_CURSTAT_CLOSED, CS_CURSTAT_DECLARED
  #		CS_CURSTAT_ROWCOUNT, CS_CURSTAT_OPEN, 
  #		CS_CURSTAT_UPDATABLE, CS_CURSTAT_RDONLY
  def cursor_state
    self.getprop( CS_CUR_STATUS)
  end

  def cursor_rows( rows )
    return self.cursor(CS_CURSOR_ROWS, nil, nil, rows)
  end

  # opt not used
  def cursor_open()
    return self.cursor(CS_CURSOR_OPEN, nil, nil, nil)
  end

  # opt:  CS_FOR_UPDATA, CS_READ_ONLY, nil(CS_UNUSED)
  def cursor_option(opt)
    return self.cursor(CS_CURSOR_OPTION, nil, nil, opt)
  end

  # opt:  CS_MORE CS_END (CTlib version >= 11 )
  def cursor_update( table, sql, opt=nil)
    return self.cursor(CS_CURSOR_UPDATE, table, sql, opt)
  end

  # opt:  CS_MORE CS_END (CTlib version >= 11 )
  def cursor_delete( table )
    return self.cursor(CS_CURSOR_DELETE, table, nil, nil)
  end

  def cursor_close
    return self.cursor(CS_CURSOR_CLOSE, nil, nil, nil)
  end

end

class SybIODesc
  def inspect
    str = String.new("SybIODesc:\n")
    hash = self.props()
    hash.each{
      |key,val|
      str.concat(" #{key} => #{val}\n")
    }
    str.concat( " timestamp len => #{hash['timestamp'].length}\n" )
    str.concat( " textptr len => #{hash['textptr'].length}\n" )
    return str
  end
end
