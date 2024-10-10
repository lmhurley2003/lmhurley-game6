#pragma once

#include <glm/glm.hpp>

#include <string>
#include <list>
#include <random>
#include <vector>

struct Connection;

//Game state, separate from rendering.

//Currently set up for a "client sends controls" / "server sends whole state" situation.

enum class Message : uint8_t {
	C2S_Controls = 1, //Greg!
	S2C_State = 's',
	//...
};

//used to represent a control input:
struct Button {
	uint8_t downs = 0; //times the button has been pressed
	bool pressed = false; //is the button pressed now
};

enum MapBlock : uint8_t {
	G = 0, //ground
	B = 1, //barrier single
	V = 2, //barrier vert
	H = 3, //barrier horiz
	UR = 4, //barrier up right corner
	UL = 5, //barrier up right corner
	DL = 6, //barrier up right corner
	DR = 7 //barrier up right corner
};

struct Map {
	std::vector<MapBlock> blocks = {
		G, G, G, G, G, G, G, G, G, G,
		G, G, G, G, G, G, G, G, G, G,
		G, G, G, G, G, G, G, G, G, G,
		G, G, G, G, G, G, G, G, G, G,
		G, G, G, G, G, G, G, G, G, G,
		G, G, G, G, G, G, G, G, G, G,
		G, G, G, G, G, G, G, G, G, G,
		G, G, G, G, G, G, G, G, G, G,
		G, G, G, G, G, G, G, G, G, G,
		G, G, G, G, G, G, G, G, G, G
	};

	uint32_t width = 10;
	uint32_t height = 10;

	MapBlock grid_idx(int x, int y) {
		return blocks[y*width + x];
	}

};

enum Direction : uint8_t {
	left,
	right,
	up,
	down
};

//state of one player in the game:
struct Player {
	//player inputs (sent from client):
	struct Controls {
		Button left, right, up, down, jump;

		void send_controls_message(Connection *connection) const;

		//returns 'false' if no message or not a controls message,
		//returns 'true' if read a controls message,
		//throws on malformed controls message
		bool recv_controls_message(Connection *connection);
	} controls;

	//player state (sent from server):
	glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);
	Direction move_dir = right;

	float velocity = 1.0f;
	float jumpVelocity = 2.0f;
	float zHeight = 0.5f;
	float next_move_timer = 1.0f / velocity;
	bool alive = true;


	std::vector<glm::ivec3> block_positions; //position of first block is effectively the player's head

	std::string name = "";
};

enum AppleType : uint8_t {
	Normal = 0
};

struct Apple {
	glm::ivec3 position;
	AppleType type;

	Apple(glm::ivec3 _p, AppleType _t) : position(_p), type(_t) {};
};

struct Game {
	std::list< Player > players; //(using list so they can have stable addresses)
	Player *spawn_player(); //add player the end of the players list (may also, e.g., play some spawn anim)
	void remove_player(Player *); //remove player from game (may also, e.g., play some despawn anim)
	
	std::list<Apple> apples;
	bool spawnApples(uint32_t applesToPlace);
	std::list<glm::ivec2> valid_spawn_positions();


	Map map{};

	std::mt19937 mt; //used for spawning players & apples
	uint32_t next_player_number = 1; //used for naming players

	Game();

	//state update function:
	void update(float elapsed);

	//constants:
	//the update rate on the server:
	inline static constexpr float Tick = 1.0f / 30.0f;
	
	//---- communication helpers ----

	//used by client:
	//set game state from data in connection buffer
	// (return true if data was read)
	bool recv_state_message(Connection *connection);

	//used by server:
	//send game state.
	//  Will move "connection_player" to the front of the front of the sent list.
	void send_state_message(Connection *connection, Player *connection_player = nullptr) const;
};
