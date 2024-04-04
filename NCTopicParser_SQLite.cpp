#include <iostream>
#include "sqlite3.h"
#include <cpr/cpr.h>
using namespace std;

//Название файла базы
const char* DB_NAME = "test.db";

//Если не существует - создание новой базы данных
//Если существует - очистка и пересоздание базы данных
void create_DB()
{
	sqlite3* db;
	int result = sqlite3_open(DB_NAME, &db);
	char* err_msg = 0;
	if (result != SQLITE_OK) 
	{
		cout << "Error: " << sqlite3_errmsg(db) << endl;
		sqlite3_close(db);
		return;
	}

	const char* sql_author = "DROP TABLE IF EXISTS 'author';"	//Убивает содержимое таблицы 'author', если она существует
		"CREATE TABLE 'author' ('id' INTEGER PRIMARY KEY AUTOINCREMENT, 'name' TEXT UNIQUE, 'topic_found' INTEGER);"	//Создаёт новую таблицу 'author' с 3 полями
		"DROP TABLE IF EXISTS 'topic';"	//Убивает содержимое таблицы 'topic', если она существует
		"CREATE TABLE 'topic' ('id' INTEGER PRIMARY KEY, 'name' TEXT, 'author_id' INTEGER, 'date' TEXT, "
		"FOREIGN KEY (author_id) REFERENCES author(id) ON DELETE CASCADE);"	//Создаёт новую таблицу 'topic' с 4 полями, связанную с таблицей 'author' по 'author.id'
		"VACUUM;";	//Очищает старое пустое место после дропов
	result = sqlite3_exec(db, sql_author, 0, 0, &err_msg);

	if (result != SQLITE_OK)	//Получение ошибки
	{
		cout << "Error creating tables: " << err_msg << endl;
		sqlite3_free(err_msg);
		sqlite3_close(db);
	}
	else cout << "Tables 'author' and 'topic' created." << endl;
}

//Обрезка данных до топиков
string cutData(string _data)
{
	string findSubStr_from = "<div class=\"entry first\">";
	string findSubStr_to = "<!--Donate-->";
	return _data.substr(_data.find(findSubStr_from), _data.find(findSubStr_to) - _data.find(findSubStr_from));
}

//Парсинг данных
void parseData()
{
	//Задаём число страниц для парсинга
	std::cout << "Type number of pages to parse: ";
	int numOfPagesToParse;
	std::cin >> numOfPagesToParse;
	if (cin.fail() || numOfPagesToParse <= 0) { std::cout << "Wrong number" << endl; cin.clear(); cin.ignore(100000, '\n'); return; }

	cpr::Url url;	//URL страницы
	string data;	//Ответ сервера, обрезанный до топиков (функцией cutData)
	string id, title, author, date;	//Распарсенные данные

	int ind_one, ind_two;		//Индексы поиска данных в полученном ответе сервера
	string find;				//Что требуется найти
	int numOfCurrentPage = 0;	//Текущий итератор страницы для формирования ссылки
	int lastAuthorID = 0;

	//"Сегодня" в строковом формате даты текущего дня
	const auto in_time_t = std::chrono::system_clock::to_time_t(chrono::system_clock::now());
	struct tm time_info;
	localtime_s(&time_info, &in_time_t);
	std::stringstream output_stream;
	output_stream << put_time(&time_info, "%Y-%m-%d");
	string today = output_stream.str();

	//Подключение к базе SQL
	sqlite3* db;
	int result = sqlite3_open(DB_NAME, &db);
	char* err_msg = 0;	//SQL-ошибка
	if (result != SQLITE_OK)
	{
		cout << "Error: " << sqlite3_errmsg(db) << endl;	//Ошибка открытия файла базы
		sqlite3_close(db);
		return;
	}

	//Парсинг страниц, если подключение было успешным
	//Проверяем на отсутствие строки "fatal_error", которая означает, что мы за пределами тем сайта,
	//а также на то, что мы не превысили заданное число страниц
	while (data.find("fatal_error") == string::npos && (numOfCurrentPage / 15) < numOfPagesToParse)
	{		
		url = "https://www.noob-club.ru/index.php?frontpage;p=" + to_string(numOfCurrentPage);
		data = cutData(cpr::Get(url).text);	//Обрезаем данные страницы до топиков
		while (data.find("entry first") != string::npos)
		{
			//id темы
			find = "index.php?topic=";
			ind_one = data.find(find) + find.length();
			ind_two = data.find(".0");
			id = data.substr(ind_one, ind_two - ind_one);
			data = data.substr(data.find("0\">") + 3);

			//title темы
			ind_two = data.find("</a></h1>");
			title = data.substr(0, ind_two);
			data = data.substr(data.find("title=") + 6);

			//author темы
			ind_two = data.find("\"");
			author = data.substr(0, ind_two);
			data = data.substr(data.find("</a> ") + 5);

			//date темы
			ind_two = data.find("</span>");
			string t_date = data.substr(0, ind_two);
			if (t_date.find("strong") != string::npos) date = today;	//Если наткнулись на жирное выделение - это значит, что вместо даты написано "Сегодня"
			else date = t_date.substr(6, 4) + "-" + t_date.substr(3, 2) + "-" + t_date.substr(0, 2);	//Разворачиваем дату в формат YYYY-MM-DD
			data = data.substr(data.find("<div class=\"entry first\">") + 12);

			//Таблица 'author'
			//Читаем 'id' автора, если он есть в таблице 'author'
			sqlite3_stmt* res;
			int currentAuthorID = 0;	//ID текущего автора
			int topicAuthorID = 0;		//ID автора для вставки в таблицу 'topic'
			string str_sql_query_getAuthorID = "SELECT id FROM author WHERE name = ?;";
			result = sqlite3_prepare_v2(db, str_sql_query_getAuthorID.c_str(), -1, &res, 0);
			if (result != SQLITE_OK) cout << "Error: " << sqlite3_errmsg(db) << endl;
			else
			{
				sqlite3_bind_text(res, 1, author.c_str(), -1, SQLITE_STATIC);
				if (sqlite3_step(res) != SQLITE_DONE) topicAuthorID = sqlite3_column_int(res, 0);
				currentAuthorID = topicAuthorID;
			}
			sqlite3_finalize(res);

			if (topicAuthorID == 0)	//Автор новый - добавляем его в таблицу author
			{
				string str_sql_query_author = "INSERT INTO author ('name', 'topic_found') VALUES ('" + author + "', '" + to_string(1) + "');";
				const char* sql_query_author = str_sql_query_author.c_str();
				result = sqlite3_exec(db, sql_query_author, 0, 0, &err_msg);
				if (result != SQLITE_OK) cout << "Error adding " << author << " in author: " << err_msg << endl;	//Получение ошибки
				lastAuthorID++;
				topicAuthorID = lastAuthorID;
			}

			//Таблица 'topic'
			//Если строка в базе уже существует - идём дальше
			const char* sql_query_topic = "INSERT INTO 'topic' ('id', 'name', 'author_id', 'date') VALUES (?, ?, ?, ?)";
			result = sqlite3_prepare_v2(db, sql_query_topic, -1, &res, 0);
			if (result == SQLITE_OK)
			{
				sqlite3_bind_int(res, 1, stoi(id));
				sqlite3_bind_text(res, 2, title.c_str(), -1, SQLITE_STATIC);
				sqlite3_bind_int(res, 3, topicAuthorID);
				sqlite3_bind_text(res, 4, date.c_str(), -1, SQLITE_STATIC);

				result = sqlite3_step(res);
				if (result == SQLITE_DONE && currentAuthorID > 0)
				{
					//Строка добавлена, автор не новый: инкриментируем запись у автора
					string str_sql_query_author = "UPDATE author SET topic_found = topic_found + 1 WHERE id = " + to_string(currentAuthorID) + ";";
					const char* sql_query_author = str_sql_query_author.c_str();
					result = sqlite3_exec(db, sql_query_author, 0, 0, &err_msg);
					if (result != SQLITE_OK) cout << "Error updating " << author << " in table 'author': " << err_msg << endl;//Получение ошибки
				}
			}
			else cout << "Error: " << sqlite3_errmsg(db) << endl;

			sqlite3_finalize(res);
		}
		numOfCurrentPage += 15;	//На одной странице 15 топиков, сдвигаем итератор на 15
		std::cout << url << " was sucsessfully parced." << endl;
	}
	sqlite3_close(db);
}

//Получение данных из базы
void getData()
{
	sqlite3* db;
	sqlite3_stmt* res;
	int result = sqlite3_open(DB_NAME, &db);
	char* err_msg = 0;

	if (result != SQLITE_OK)	//Ошибка открытия файла базы
	{
		cout << "Error: " << sqlite3_errmsg(db) << endl;
		sqlite3_close(db);
		return;
	}

	string userInput;
	while (true)
	{
		cout << "Type \"Topics\" to view topics." << endl;
		cout << "Type \"Authors\" to view authors." << endl;
		cout << "Type \"Back\" to get to previous menu." << endl;
		cin >> userInput;

		//Просмотр тем
		if (userInput == "Topics" || userInput == "topics")
		{
			//Число топиков для просмотра
			int numOfTopicsToView;
			cout << "Type number of topics to view (or 0 to view all): ";
			cin >> numOfTopicsToView;
			if (cin.fail() || numOfTopicsToView < 0) { std::cout << "Wrong number" << endl; cin.clear(); cin.ignore(100000, '\n'); continue; }
			if (numOfTopicsToView == 0) numOfTopicsToView = -1;

			const char* sql_query = "SELECT topic.id, topic.name, author.name, topic.date FROM topic JOIN author ON author.id = topic.author_id LIMIT ?";
			result = sqlite3_prepare_v2(db, sql_query, -1, &res, 0);

			if (result == SQLITE_OK)
			{
				cout << "=================================" << endl;
				sqlite3_bind_int(res, 1, numOfTopicsToView);
				while (sqlite3_step(res) == SQLITE_ROW)
				{
					cout << "ID " << sqlite3_column_int(res, 0) << ": ";	//ID темы, не строки в базе
					cout << sqlite3_column_text(res, 1) << endl;		//Заголовок темы
					cout << "Author: " << sqlite3_column_text(res, 2) << " ";		//Автор
					cout << "from " << sqlite3_column_text(res, 3) << endl << endl;	//Дата
				}
				cout << "=================================" << endl;
			}
			else cout << "Error: " << sqlite3_errmsg(db) << endl;

			sqlite3_finalize(res);
		}

		//Просмотр авторов
		else if (userInput == "Authors" || userInput == "authors")
		{
			//Число авторов для просмотра
			int numOfAuthorsToView;
			cout << "Type number of authors to view (or 0 to view all): ";
			cin >> numOfAuthorsToView;
			if (cin.fail() || numOfAuthorsToView < 0) { std::cout << "Wrong number" << endl; cin.clear(); cin.ignore(100000, '\n'); continue; }
			if (numOfAuthorsToView == 0) numOfAuthorsToView = -1;

			const char* sql_query = "SELECT * FROM 'author' LIMIT ?";
			result = sqlite3_prepare_v2(db, sql_query, -1, &res, 0);

			if (result == SQLITE_OK)
			{
				cout << "=================================" << endl;
				sqlite3_bind_int(res, 1, numOfAuthorsToView);
				while (sqlite3_step(res) == SQLITE_ROW)
				{
					cout << sqlite3_column_int(res, 0) << ". ";		//ID автора в таблице
					cout << sqlite3_column_text(res, 1) << endl;	//Имя автора
					cout << "Number of topics: " << sqlite3_column_int(res, 2) << " " << endl << endl;	//Число тем автора
				}
				cout << "=================================" << endl;
			}
			else cout << "Error: " << sqlite3_errmsg(db) << endl;

			sqlite3_finalize(res);
		}

		else if (userInput == "Back" || userInput == "back") break;			//Выход в предыдущее меню
		else cout << "Error typing" << endl;	//Ошибка ввода
	}
	sqlite3_close(db);
}

int main()
{
	//Настройка консоли на кодировку UTF8 для вывода русского языка
	SetConsoleOutputCP(CP_UTF8);

	string userInput;
	while (true)
	{
		cout << "Type \"Create\" to create new DB." << endl;
		cout << "Type \"Parse\" to parse data." << endl;
		cout << "Type \"View\" to view data if it exists." << endl;
		cout << "Type \"Exit\" to exist." << endl;

		cin >> userInput;
		if (userInput == "Create" || userInput == "create") create_DB();	//(Пере-)создание базы данных
		else if (userInput == "Parse" || userInput == "parse") parseData();	//Парсим сайт
		else if (userInput == "View" || userInput == "view") getData();		//Просмотр результата
		else if (userInput == "Exit" || userInput == "exit") break;			//Выход
		else cout << "Error typing" << endl;	//Ошибка ввода
	}
}