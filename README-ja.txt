						     2001ǯ 3��20�� 
========
�� ��
========

  �ܥ⥸�塼��ϡ�ruby ���� SyBase SQL�����Ф򥢥��������뤿��γ�ĥ��
���֥��Ǥ���

  SyBase�����ФȤΥ��󥿡��ե������ˤϡ������١������饤����ȥ饤�֥�
�꡼����Ѥ��Ƥ��ޤ���

  ����������ʬ�λȤ��ϰϤ����������Ƥ��ޤ����äˡ�
        * ��ĥ���顼��å������ν���
	* �����ʥߥå� SQL
	* RDBC �μ���
�ʤɡ��ͤˤ�äƤϽ��פȻפ�����ʬ������Ƥ��ޤ���

==========
���Ѿ��
==========
  Ruby ���Τ����۾��˽��äƲ�������

==========
ư����
==========
  ruby-1.3.X �ʾ�ȡ�SyBase �����ץ󥯥饤����ȥ饤�֥�꡼��ɬ�פǤ���
  
========
INSTALL
========

1. ���餫���ᡢruby-1.3.X�ʾ� �ȡ�SyBase �򥤥󥹥ȡ��뤷�Ƥ����Ƥ���������

    Linux �Ǥ�SyBase �ˤĤ���
    -----------------------------
       Linux �� Adaptive Server Enterprise 11.0.3�ξ�硢���ܸ�Ķ��ǻ���
      ����ˤϡ�RDBMS ���� ���ɲå⥸�塼��ե�����(locales-diff.tar.gz)��
      �󥹥ȡ��뤷�����ȡ�
	      /opt/sybase/locales/locales.dat 
      �� [linux] �ι��ܤˡ��ʲ��Υ���ȥ꡼���ɲä��Ƥ���������
	      locale = ja_JP.ujis, japanese, eucjis
              locale = ja_JP.EUC-JP, japanese, eucjis

2. ���������֤�Ÿ��

  % gzip -d <sybct-ruby-0.2.7.tar.gz | tar xvf -


3. sybct-ruby-0.2.7 �ǥ��쥯�ȥ�˰�ư

  % cd sybct-ruby-0.2.7

4. ɬ�פʤ�С�extconf.rb �������Ƥ���������

  extconf.rb �� linux + gcc + ASE12.5 + ruby1.8  �Ѥ����ꤷ�Ƥ��ޤ���
  ¾�δĶ��ξ��ϡ�
    sybase = "/opt/sybase-12.5"
    sybase_ocs = "#{sybase}/OCS-12_5"
    $CFLAGS $LDFLAGS  $LOCAL_LIBS 
�ε��Ҥ���Ѥ���Ķ��˹�碌���ѹ����Ƥ���������

  ---- ���: Ruby 1.9.1 ----
    Ruby 1.9.1 ���顢�ͥ��ƥ��֥���åɤ����ݡ��Ȥ���ޤ����Τǡ��ꥨ
  ��ȥ��Ȥ�Sybase�饤�֥�꡼ (***_r.so�Ȥ���̾��)���󥯤��Ʋ���
  ����
    (��)
      Intel-32bit-Linux + ASE12.5 + ruby1.9.1 �δĶ��ʤ顢
      $LOCAL_LIBS = "-lct_r -lcs_r -lsybtcl_r -lcomn_r -lintl_r -rdynamic -ldl -lnsl -lm"    


5. Makefile �κ���
 % ruby extconf.rb

6. make 
 % make


7. �ƥ��ȥ���ץ�μ¹�

  ���Ǥˤɤ����� SyBase SQL �����Ф�ư���Ƥ���С��ʲ��Υ�����ץȤ�ư
���ǧ����ǽ�Ǥ���

    % echo $SYBASE ; echo $SYBASE_OCS  # check the $SYBASE environment variable
    % LD_LIBRARY_PATH="${SYBASE}/${SYBASE_OCS}/lib" ; export LD_LIBRARY_PATH
    % cd sample
    % ruby -I ../ ./sqlsample.rb -S dbserver -U sa -P XXXXXX 
    % ruby -I ../ ./rpcsample.rb  -S dbserver -U sa -P XXXXXX 

  dbserver �ˤϻ��Ѥ��� SyBase�����Ф�̾������ꤷ�Ƥ���������XXXXXX�� 
sa �桼���Υѥ���ɤǤ���
    

8. ���󥹥ȡ���

  sybct.o sybct.so sybct.rb sybsql.rb �� $LOAD_PATH �Τɤ����˥��ԡ���
�Ƥ���������

  �㤨�С�
  % su
  $ cp sybct.o sybct.so sybct.rb sybsql.rb /usr/local/lib/ruby/site_ruby/1.8/i686-linux/
�Ǥ���


=======
�Ȥ���
=======
  �ƥ��饹���������Ƥ���᥽�åɤξܺ٤ˤĤ��Ƥϡ�doc/index.html ��
�ߤƤ��������� �����Ǥϡ��Ȥꤢ�������Ѥ��뤿��δ�ñ��������Ԥʤ���
����

* �ե졼�� *
--------------
  �ʲ��ˡ���ɽŪ�ʥץ���ߥ󥰼��򼨤��ޤ��Τǡ����ͤˤ��Ƥ���������
  
(1) �饤�֥�꡼�Υ���
  require "sybsql.rb"

(2) ��å�����������Хå��κ����
  ɬ�פʤ�С����ѤΥ�å�����������Хå��᥽�åɤ��������ޤ���

  SybSQLContext ���饹�ˡ�
	srvmsgCB	----	�����Х�å�����������Хå�
	cltmsgCB	----    ���饤����ȥ�å�����������Хå�
����ĤΥ�å�����������Хå��ѥ᥽�åɤ��������Ƥ��ޤ��Τǡ�������
�Ĥ�Ŭ���˺�������Ƥ���������
  �ܺ٤ϡ�sybsql.rb ������������ �ڤ� doc/SybSQLContext.html ��ߤƤ���������

  ���⤷�ʤ���С��ǥե���ȤΥ�����Хå������Τޤ�Ŭ�Ѥ���ޤ���

(3) �����ФȤ���³���Ω���롣
  q = SybSQL.new(hash) �� ������ SybSQL���󥹥��� q ��������Ƥ�����
�������λ����ǡ�SQL�����ФȤ���³����Ω����ޤ���

  hash �ˤϡ�������̾����������Ⱦ��󡢸������ �ʤɤ���ꤷ�ޤ���
  ��ʥ����ˤϡ�

	'S' => '������̾
	'U' => 'login �桼��̾'
	'P' => '�ѥ����'
        'lang' => 'locales.dat ����Ͽ����Ƥ��������̾'
	'timeout' => ��³�����ॢ������
        'async' => �ޥ������åɥ��ץꥱ�������ξ�硢 true ������ 
                  �ʾ�ά����false��

�ʤɤ�����ޤ����ܺ٤ϡ�doc/SybSQL.html ��ߤƤ���������

  ��³�ϡ�q.close �᥽�åɤ�ȯ�Ԥ��뤫���⤷���ϡ�SybSQL���󥹥���
(���ξ��� q�ˤ�Ruby �ˤ�äƥ��١������쥯����󤵤��ޤ�ͭ���Ǥ���

(4) SQL��ȯ��
  select ʸ�Τ褦�ˡ������֤�SQL���ޥ�ɤ�ȯ�Ԥˤϡ�SybSQL ���饹�� 
sql�᥽�åɤ�ȤäƤ���������
  insert, use db �ʤɤΥ����֤��ʤ����ޥ�ɤˤĤ��Ƥϡ�sql_norow ��
���åɤ���Ѥ���������ñ�˽����Ǥ��ޤ���

(5) SQLȯ�Ԥ����ݳ�ǧ
  sql_norow �᥽�åɤˤĤ��Ƥϡ������֤��ͤ� true �ʤ�С������������
�Ƥ��ޤ���
  sql �᥽�åɤη�̤ϡ�ľ��� cmd_done? �᥽�åɤ��֤���(true / false
)��Ƚ�Ǥ��ޤ���

(6) ��̤μ���
  sql ����� sql_norow �᥽�åɤη�̤ϡ�SybResult ���֥������Ȥ�����
�Ȥ��ơ�SybSQL�Υ��󥹥����ѿ��˳�Ǽ����Ƥ��ޤ���

  ��������ϡ�SybSQL��results�᥽�åɤǼ��Ф����Ȥ��Ǥ��ޤ���������
�Ƥ��ϡ�SybSQL�� top_row_result�᥽�åɤ���Ѥ��ޤ���
  top_row_result �ϡ���̥ǡ�������Τ������ǽ���̾����̤��֤���
���åɤǤ������äơ�sql�᥽�åɤ�ʣ����selectʸ����ꤷ���ꡢʣ�����
�ǡ������֤��ü�ʥ��ȥ��ɥץ��������ꤷ�ʤ��¤�ͭ���Ǥ���

  SybResult ���󶡤��Ƥ��롢��̥ǡ����򥢥��������뤿��Ρ���ʥ᥽��
�ɤˤϡ�

  rows
	����̤�����������פȤ����֤��ޤ���
	[ [�����Υǡ��� .... ] [�����Υǡ��� .... ] ...[����Υǡ��� .... ] ]

  nthrow(nth, col=nil )
	col����ά���줿���
		nth ���ܤη�̥�������֤��ޤ�  [��nth �Υǡ��� .... ]
	col�������ʤ顢
		nth ���ܤη�̥��Ρ�col������ܤΥǡ������֤��ޤ���
	col��ʸ����ʤ顢
		nth ���ܤη�̥��Ρ�'col�����'�Υǡ������֤��ޤ���
  columns
       ����̤Υ��������String�Ρ�����פȤ����֤��ޤ�

�ʤɤ�����ޤ���

(7) ��³�Υ�����
  SybSQL �� close �᥽�åɤ�ȯ�Ԥˤ�ꡢ��³�����Ǥ��ޤ���
  ������³����ˤϡ�(3) ����äƿ����� SybSQL���֥������Ȥ��������ʤ�
��Фʤ�ޤ���


* ñ��ʡ�SQLȯ�ԥץ�������
---------------------------------
  �ʲ��ˡ��ºݤΥץ������򼨤��ޤ���

      require "sybsql.rb"  # ��ĥ�饤�֥�꡼�Υ���
      begin

	# ������̾ SYBASE�ˡ� �桼�� sa, �ѥ���� 'PassWord' ����³
	query=SybSQL.new( {'S'=>'SYBASE', 'U'=>'sa', 'P'=>'PassWord'} )

	# mydb �ǡ����١��� ��Ȥ�
	raise "ERROR use mydb" unless( query.sql_norow("use mydb") )

	# SQLȯ��
	query.sql("select * from theTable")
	# ERROR �����å�
	raise "ERROR SELECT" unless (query.cmd_done? )

	# �ǽ���̾����̥��åȤ� res �˼���
	res = query.top_row_result

	# �����̾����Υץ���  
	print res.columns.join(','), "\n"

	# ���ǡ����Υץ���
	res.rows.each {
	  |r| print r.join('|'), "\n"
	}

      rescue
	# ���顼ɽ��
	print $!,"\n"

      ensure
	# ��³�����Ǥ������饤����ȥ饤�֥�꡼�ν�λ
	query.close if(query.kind_of?( SybSQL ) )
      end

=====================
����ץ�ˤĤ���
=====================
  sample/ �ǥ��쥯�ȥ꡼�� �ʲ��Υ���ץ륹����ץȤ�����ޤ��Τǡ���
����ߥ󥰤λ��ͤˤǤ⤷�Ƥ���������

getimage.rb
------------
  Image �ǡ����θ�����Ԥʤ�����ץ�Ǥ���
  SyBase�Υ���ץ�ǡ����١��� pubs2 �� au_pix �ơ��֥����Ͽ���Ƥ���
���ƤΥ��᡼���ǡ����� �ǥ������ե��������ޤ���
  �Ȥ����ϡ�getimage.rb ����Ƭ�Υ����Ȥ򸫤Ƥ���������
  
sendimage.rb
------------
  Image �ǡ����� �ɲá����� ��Ԥʤ�����ץ�Ǥ���
  SyBase�Υ���ץ�ǡ����١��� pubs2 �� au_pix �ơ��֥�ˡ����ꤷ����
�᡼���ե��������Ͽ���ޤ���
  �Ȥ����ϡ�sendimage.rb ����Ƭ�Υ����Ȥ򸫤Ƥ���������

cursor_disp.rb, cursor_update.rb
------------------------------------
  Client-Library ��������Υ���ץ�Ǥ���
  �¹Ԥˤ� pubs2 �ǡ����١�����ɬ�פǤ���

isql.rb
--------
  isql ���ޥ�ɤΤ褦�ʤ�ΤǤ���
  �¹Ԥˤϡ�ruby �� readline �⥸�塼�뤬ɬ�פǤ���
  �Ȥ����ϡ�isql.rb ����Ƭ�Υ����Ȥ򸫤Ƥ���������

======================
ư���ǧ����Ƥ���Ķ�
======================
  ���ΤȤ���
	OS:		Linux (Vine Linux 3.0)
	Ruby:		ruby 1.4.2 , ruby 1.6.7, ruby 1.8.2
        Sybase:		Adaptive Server Enterprise 11.0.3E6 ����� 12.5
�δĶ��Ǥ�����ǧ���Ƥ��ޤ���

======================================
sybct-ruby-0.2.11 ==> sybct-ruby-0.2.12
======================================
* Fixed compile error on Ruby 1.9.1.
  (Thanks to Bob Saveland )

======================================
sybct-ruby-0.2.10 ==> sybct-ruby-0.2.11
======================================
* applied Ruby-DBI patches from Charles.  thanks.
  - allow_serveraddr_prop.patch (this is to allow connection by
    specification of CS_SERVERADDR);
  - expose_column_types.patch (to allow introspection on column types
    of result sets); and
  - extra_defines_for_freetds.patch (these were neccessary to compile
    with freetds).

* => sybct-ruby-0.2.11_p1
  *extconf.rb 
    ASE15.0.3 Linux 64bit compile options. 
    (Thanks to Martin Jezek )

======================================
sybct-ruby-0.2.9 ==> sybct-ruby-0.2.10
======================================
* extconf.rb ���ѹ�
  * 64bit linux �Υ���ѥ��륪�ץ������ɲ� 
    (Thanks to Hiroyuki Sato and Kiriakos Georgiou)
  * Mac OSX(tiger) �� freetds(ver 0.64)  �Υ���ѥ��륪�ץ��������ɲ� 

=====================================
sybct-ruby-0.2.8 ==> sybct-ruby-0.2.9
=====================================
* freetds �Υ��ݡ��� (freetds-0.64RC3 �Ǥ���ư���ǧ���Ƥ��ޤ���)
  �ޤ��ƥ��ȼ������ʳ��Ǥ��������Ĥ��ε�ǽ��ư��ޤ���
  �ܺ٤ϱѸ��README.txt��ߤƤ���������

* README-cygwin.txt
  cygwin�Ǥ� make��ˡ��񤤤Ƥߤޤ�����

==================================================
sybct-ruby-0.2.7 ==> sybct-ruby-0.2.8 �μ���ѹ���
==================================================
* extconf.rb
  Mac OS X 10.4 �Ѥε��� (By Scott McKenzie)

* �ǿ��� CS_VERSION_nnn ��Ȥ��褦���ѹ� ( Thanks to Will Sobel )
  

==================================================
sybct-ruby-0.2.4 ==> sybct-ruby-0.2.7 �μ���ѹ���
==================================================
* extconf.rb
  ASE12.5 �ξ��ε��Ҥ��ɲ�

* SybSQL#new 'login-timeout' °��
* SybSQL#connection_status �ɲ�
* SybSQL#connection_dead?  �ɲ�

* �����Ĥ���BUG�餷����Τ���

=============================================
sybct-0.2.2 ==> sybct-ruby-0.2.4 �μ���ѹ���
=============================================
* Client-Library��٥�Υ�����������˰����б���
  �ʲ��ε�ǽ��̤�б�
    + ct_keydata��Ȥä������ȥ��κ����
    + ��������κƥ����ץ�
    + �ۥ����ѿ�����

* SybContext.create �Ǥ� async ���ץ������ɲ�
  sybase API �Υͥåȥ����������ˤ����Ƥ⡢ruby����åɤΥ���ƥ�
���ȥ����å����ڤ��ؤ��褦�ˤ��ޤ�����

=========================================
sybct-0.2.0 ==> sybct-0.2.2 �μ���ѹ���
=========================================

* CTlib �� ct_get_data/ct_send_data ����Ѥ��� Image/Text�ǡ�����ž��
��ǽ�������

* SybCommand.fetch �� ñ�����fetch���ѹ�(ct_fetch�ˤ��᤯�����ˡ�
  �����SybCommand.fetch �ε�ǽ�ϡ� SybCommand.fetchloop �����ء�

* SybSQL.sql �᥽�åɤˡ�sql{|args..|    } �����ǡ��������ĸƤӽФ�
���֥�å������Ǥ���褦�ˤ�����
  �ʵ�����Υե��å��ȡ�CS_IODESC�����Τ����

* ���դ򤤤Ĥ� yyyy/mm/dd hh:mm:ss �����ǥե��å�����褦�ˤ�����
  ��0.2.0 �Ǥ� locale������ˤ�äƤϡ����־��󤬥ե��å��Ǥ��ʤ��ä�����

* SybReslt �� �ȥ�󥶥�����󥹥ơ������ȥ�������Ȥ򸡺�����᥽��
�ɤ��ɲ�

=======
���
=======

����ů��

�Х���ݡ��Ȥʤɤ� tetsuhumi@aa.bb-east.ne.jp  �ޤǤ��ꤤ���ޤ���

===========
NO WARRANTY
===========
  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.

