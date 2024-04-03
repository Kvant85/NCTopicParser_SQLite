#include <iostream>
#include "sqlite3.h"
#include <cpr/cpr.h>
using namespace std;

//Название файла базы
const char* DB_NAME = "test.db";

//Если не существует - создание новой базы данных
//Если существует - очистка и пересоздание базы данных
void create_table()
{
	sqlite3* db;
	int result = sqlite3_open(DB_NAME, &db);
	char* err_msg = 0;
	if (result != SQLITE_OK) 
	{
		cout << "Error: " << sqlite3_errmsg(db);
		sqlite3_close(db);
		return;
	}

	const char* sql = "DROP TABLE IF EXISTS 'topic';"	//Убивает содержимое таблицы, если оно существует
		"CREATE TABLE 'topic' ('id' INTEGER PRIMARY KEY, 'name' TEXT, 'author' TEXT, 'date' TEXT);"	//Создаёт новую таблицу с 4 полями
		"VACUUM";
	result = sqlite3_exec(db, sql, 0, 0, &err_msg);

	if (result != SQLITE_OK)	//Получение ошибки
	{
		cout << "Error creating table: " << err_msg;
		sqlite3_free(err_msg);
		sqlite3_close(db);
	}
	else cout << "Table created." << endl;
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
	if (cin.fail() || numOfPagesToParse <= 0) { std::cout << "Wrong number" << endl; return; }

	cpr::Url url;	//URL страницы
	string data;	//Ответ сервера, обрезанный до топиков (функцией cutData)
	string id, title, author, date;	//Распарсенные данные

	int ind_one, ind_two;		//Индексы поиска данных в полученном ответе сервера
	string find;				//Что требуется найти
	int numOfCurrentPage = 0;	//Текущий итератор страницы для формирования ссылки

	//"Сегодня" в строковом формате даты текущего дня
	const auto in_time_t = std::chrono::system_clock::to_time_t(chrono::system_clock::now());
	struct tm time_info;
	localtime_s(&time_info, &in_time_t);
	std::stringstream output_stream;
	output_stream << put_time(&time_info, "%d-%m-%Y");
	string today = output_stream.str();

	//Парсинг страниц
	//Проверяем на отсутствие строки "fatal_error", которая означает, что мы за пределами тем сайта,
	//а также на то, что мы не превысили заданное число страниц
	while (data.find("fatal_error") == string::npos && (numOfCurrentPage / 15) < numOfPagesToParse)
	{		
		url = "https://www.noob-club.ru/index.php?frontpage;p=" + to_string(numOfCurrentPage);
		data = cutData(cpr::Get(url).text);	//Обрезаем данные до топиков

		while (data.find("entry first") != string::npos)
		{
			//Собираем данные в SQL-запрос
			//Делаем новое подключение на каждый запрос, так как в противном случае SQLite шикарно линейно высасывает память
			sqlite3* db;
			int result = sqlite3_open(DB_NAME, &db);	//Подключение к базе
			char* err_msg = 0;	//SQL-ошибка
			if (result != SQLITE_OK) 
			{
				cout << "Error: " << sqlite3_errmsg(db);	//Ошибка открытия файла базы
				sqlite3_close(db);
				return;
			}

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
			date = data.substr(0, ind_two);
			if (date.find("strong") != string::npos)
				date = today;
			data = data.substr(data.find("<div class=\"entry first\">") + 12);

			//Параметризируем запрос, так как в названиях топиков встречаются ломающие прямой запрос символы - '
			//Если строка в базе уже существует - идём дальше
			const char* sql_query = "INSERT OR IGNORE INTO 'topic' ('id', 'name', 'author', 'date') VALUES (?, ?, ?, ?)";
			sqlite3_stmt* res;
			result = sqlite3_prepare_v2(db, sql_query, -1, &res, 0);

			if (result == SQLITE_OK)
			{
				sqlite3_bind_int(res, 1, stoi(id));
				sqlite3_bind_text(res, 2, title.c_str(), -1, SQLITE_STATIC);
				sqlite3_bind_text(res, 3, author.c_str(), -1, SQLITE_STATIC);
				sqlite3_bind_text(res, 4, date.c_str(), -1, SQLITE_STATIC);

				int step = sqlite3_step(res);
			}
			else cout << "Error: " << err_msg << endl;

			sqlite3_finalize(res);
			sqlite3_close(db);
		}
		numOfCurrentPage += 15;	//На одной странице 15 топиков, сдвигаем итератор на 15
		std::cout << url << " was sucsessfully parced." << endl;
	}
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
		cout << "Error: " << sqlite3_errmsg(db);
		sqlite3_close(db);
		return;
	}

	//Число топиков для просмотра
	int numOfTopicsToView;
	cout << "Type number of topics to view (or 0 to view all): ";
	cin >> numOfTopicsToView;
	if (cin.fail() || numOfTopicsToView < 0) { std::cout << "Wrong number" << endl; return; }
	if (numOfTopicsToView == 0) numOfTopicsToView = -1;

	const char* sql_query = "SELECT * FROM 'topic' LIMIT ?";
	result = sqlite3_prepare_v2(db, sql_query, -1, &res, 0);

	if (result == SQLITE_OK)
	{
		sqlite3_bind_int(res, 1, numOfTopicsToView);
		while (sqlite3_step(res) == SQLITE_ROW)
		{
			cout << "ID " << sqlite3_column_int(res, 0) << ": ";	//ID темы, не строки в базе
			cout << "" << sqlite3_column_text(res, 1) << endl;		//Заголовок темы
			cout << "Author: " << sqlite3_column_text(res, 2) << " ";		//Автор
			cout << "from " << sqlite3_column_text(res, 3) << endl << endl;	//Дата
		}
	}
	else cout << "Error: " << sqlite3_errmsg(db);
	sqlite3_finalize(res);
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
		if (userInput == "Create" || userInput == "create") create_table();	//(Пере-)создание таблицы
		else if (userInput == "Parse" || userInput == "parse") parseData();	//Парсим сайт
		else if (userInput == "View" || userInput == "view") getData();		//Просмотр результата
		else if (userInput == "Exit" || userInput == "exit") break;			//Выход
		else cout << "Error typing" << endl;	//Ошибка ввода
	}
}