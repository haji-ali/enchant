/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* enchant
 * Copyright (C) 2003 Joan Moratinos <jmo@softcatala.org>, Dom Lachowicz
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * In addition, as a special exception, Dom Lachowicz
 * gives permission to link the code of this program with
 * non-LGPL Spelling Provider libraries (eg: a MSFT Office
 * spell checker backend) and distribute linked combinations including
 * the two.  You must obey the GNU General Public License in all
 * respects for all of the code used other than said providers.  If you modify
 * this file, you may extend this exception to your version of the
 * file, but you are not obligated to do so.  If you do not wish to
 * do so, delete this exception statement from your version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h> 

#include <string>
#include <vector>

#include "myspell_checker.h"
#include "enchant.h"
#include "enchant-provider.h"

#define G_ICONV_INVALID (GIConv)-1

/***************************************************************************/

static char *
myspell_checker_get_prefix (void)
{
	char * data_dir = NULL;

	data_dir = enchant_get_registry_value ("Myspell", "Data_Dir");
	if (data_dir)
		return data_dir;

#ifdef ENCHANT_MYSPELL_DICT_DIR
	return g_strdup (ENCHANT_MYSPELL_DICT_DIR);
#else
	return NULL;
#endif
}

static bool
g_iconv_is_valid(GIConv i)
{
	return (i != G_ICONV_INVALID);
}

MySpellChecker::MySpellChecker()
	: myspell(0), m_translate_in(G_ICONV_INVALID), m_translate_out(G_ICONV_INVALID)
{
}

MySpellChecker::~MySpellChecker()
{
	delete myspell;
	if (g_iconv_is_valid (m_translate_in ))
		g_iconv_close(m_translate_in);
	if (g_iconv_is_valid(m_translate_out))
		g_iconv_close(m_translate_out);
}

bool
MySpellChecker::checkWord(const char *utf8Word, size_t len)
{
	if (len > MAXWORDLEN || !g_iconv_is_valid(m_translate_in))
		return false;

	char *in = (char*) utf8Word;
	char word8[MAXWORDLEN + 1];
	char *out = word8;
	size_t len_in = len * sizeof(char);
	size_t len_out = sizeof( word8 ) - 1;
	g_iconv(m_translate_in, &in, &len_in, &out, &len_out);
	*out = '\0';
	if (myspell->spell(word8))
		return true;
	else
		return false;
}

char**
MySpellChecker::suggestWord(const char* const utf8Word, size_t len, size_t *nsug)
{
	if (len > MAXWORDLEN 
		|| !g_iconv_is_valid(m_translate_in)
		|| !g_iconv_is_valid(m_translate_out))
		return 0;
	char *in = (char*) utf8Word;
	char word8[MAXWORDLEN + 1];
	char *out = word8;
	size_t len_in = len;
	size_t len_out = sizeof(word8) - 1;
	g_iconv(m_translate_in, &in, &len_in, &out, &len_out);
	*out = '\0';
	char **sugMS;
	*nsug = myspell->suggest(&sugMS, word8);
	if (*nsug > 0) {
		char **sug = g_new0 (char *, *nsug + 1);
		for (int i=0; i<*nsug; i++) {
			in = sugMS[i];
			len_in = strlen(in);
			len_out = sizeof(char) * (len_in + 1);
			char *word = g_new0(char, len_out);
			out = reinterpret_cast<char *>(word);
			g_iconv(m_translate_out, &in, &len_in, &out, &len_out);
			*(out) = 0;
			sug[i] = word;
			free(sugMS[i]);
		}
		free(sugMS);
		return sug;
	}
	else
		return 0;
}

static void
s_buildHashNames (std::vector<std::string> & names, const char * dict)
{
	char * tmp, * private_dir, * home_dir, * myspell_prefix, * dict_dic;

	names.clear ();

	dict_dic = g_strconcat(dict, ".dic", NULL);

	home_dir = enchant_get_user_home_dir ();
	if (home_dir) {
		private_dir = g_build_filename (home_dir, ".enchant", 
						"myspell", NULL);
		
		tmp = g_build_filename (private_dir, dict_dic, NULL);
		names.push_back (tmp);
		g_free (tmp);

		g_free (private_dir);
		g_free (home_dir);
	}

	myspell_prefix = myspell_checker_get_prefix ();
	if (myspell_prefix) {
		tmp = g_build_filename (myspell_prefix, dict_dic, NULL);
		names.push_back (tmp);
		g_free (tmp);
		g_free (myspell_prefix);
	}

	g_free(dict_dic);
}

static char *
myspell_request_dictionary (const char * tag) 
{
	char * dic = NULL;

	std::vector<std::string> names;

	s_buildHashNames (names, tag);

	for (size_t i = 0; i < names.size () && !dic; i++) {
		if (g_file_test(names[i].c_str(), G_FILE_TEST_EXISTS))
			dic = g_strdup (names[i].c_str());
	}
	
	return dic;
}

bool
MySpellChecker::requestDictionary(const char *szLang)
{
	const char *dictBase = NULL;
	char *dic = NULL, *aff = NULL;
	char *home_dir = enchant_get_user_home_dir();

	dic = myspell_request_dictionary (szLang);
	if (!dic) {
		std::string shortened_dict (szLang);
		size_t uscore_pos;
		
		// try abbreviated form
		if ((uscore_pos = shortened_dict.rfind ('_')) != ((size_t)-1)) {
			shortened_dict = shortened_dict.substr(0, uscore_pos);
			dic = myspell_request_dictionary (shortened_dict.c_str());
		}
	}
	if (!dic)
		return false;

	aff = g_strdup(dic);
	int len_dic = strlen(dic);
	strcpy(aff+len_dic-3, "aff");
	myspell = new MySpell(aff, dic);
	g_free(dic);
	g_free(aff);
	char *enc = myspell->get_dic_encoding();

	m_translate_in = g_iconv_open(enc, "UTF-8");
	m_translate_out = g_iconv_open("UTF-8", enc);

	return true;
}

/*
 * Enchant
 */

static char **
myspell_dict_suggest (EnchantDict * me, const char *const word,
		     size_t len, size_t * out_n_suggs)
{
	MySpellChecker * checker;
	
	checker = (MySpellChecker *) me->user_data;
	return checker->suggestWord (word, len, out_n_suggs);
}

static int
myspell_dict_check (EnchantDict * me, const char *const word, size_t len)
{
	MySpellChecker * checker;
	
	checker = (MySpellChecker *) me->user_data;
	
	if (checker->checkWord(word, len))
		return 0;
	
	return 1;
}

static void
myspell_dict_free_suggestions (EnchantDict * me, char **str_list)
{
	g_strfreev (str_list);
}

static EnchantDict *
myspell_provider_request_dict(EnchantProvider * me, const char *const tag)
{
	EnchantDict *dict;
	MySpellChecker * checker;
	
	checker = new MySpellChecker();
	
	if (!checker)
		return NULL;
	
	if (!checker->requestDictionary(tag)) {
		delete checker;
		return NULL;
	}
	
	dict = g_new0(EnchantDict, 1);
	dict->user_data = (void *) checker;
	dict->check = myspell_dict_check;
	dict->suggest = myspell_dict_suggest;
	dict->free_suggestions = myspell_dict_free_suggestions;
	// don't implement personal, session
	
	return dict;
}

static void
myspell_provider_dispose_dict (EnchantProvider * me, EnchantDict * dict)
{
	MySpellChecker *checker;
	
	checker = (MySpellChecker *) dict->user_data;
	delete checker;
	
	g_free (dict);
}

static EnchantDictStatus
myspell_provider_dictionary_status (struct str_enchant_provider * me,
				    const char *const tag)
{
	std::vector <std::string> names;

	s_buildHashNames (names, tag);
	for (size_t i = 0; i < names.size(); i++) {
		if (g_file_test (names[i].c_str(), G_FILE_TEST_EXISTS))
			return EDS_EXISTS;
	}

	std::string shortened_dict (tag);
	size_t uscore_pos;
	
	if ((uscore_pos = shortened_dict.rfind ('_')) != ((size_t)-1)) {
		shortened_dict = shortened_dict.substr(0, uscore_pos);

		s_buildHashNames (names, shortened_dict.c_str());
		for (size_t i = 0; i < names.size(); i++) {
			if (g_file_test (names[i].c_str(), G_FILE_TEST_EXISTS))
				return EDS_EXISTS;
		}
	}

	return EDS_DOESNT_EXIST;
}

static void
myspell_provider_dispose (EnchantProvider * me)
{
	g_free (me);
}

static char *
myspell_provider_identify (EnchantProvider * me)
{
	return "myspell";
}

static char *
myspell_provider_describe (EnchantProvider * me)
{
	return "Myspell Provider";
}

extern "C" {

ENCHANT_MODULE_EXPORT (EnchantProvider *) 
init_enchant_provider (void)
{
	EnchantProvider *provider;
	
	provider = g_new0(EnchantProvider, 1);
	provider->dispose = myspell_provider_dispose;
	provider->request_dict = myspell_provider_request_dict;
	provider->dispose_dict = myspell_provider_dispose_dict;
	provider->dictionary_status = myspell_provider_dictionary_status;
	provider->identify = myspell_provider_identify;
	provider->describe = myspell_provider_describe;

	return provider;
}

} // extern C linkage