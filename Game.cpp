#include "Game.hpp"

#include "Connection.hpp"

#include <stdexcept>
#include <iostream>
#include <cstring>

#include <glm/gtx/norm.hpp>

void Player::Controls::send_controls_message(Connection *connection_) const {
	assert(connection_);
	auto &connection = *connection_;

	uint32_t size = 5;
	connection.send(Message::C2S_Controls);
	connection.send(uint8_t(size));
	connection.send(uint8_t(size >> 8));
	connection.send(uint8_t(size >> 16));

	auto send_button = [&](Button const &b) {
		if (b.downs & 0x80) {
			std::cerr << "Wow, you are really good at pressing buttons!" << std::endl;
		}
		connection.send(uint8_t( (b.pressed ? 0x80 : 0x00) | (b.downs & 0x7f) ) );
	};

	send_button(left);
	send_button(right);
	send_button(up);
	send_button(down);
	send_button(jump);
}

bool Player::Controls::recv_controls_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;

	auto &recv_buffer = connection.recv_buffer;

	//expecting [type, size_low0, size_mid8, size_high8]:
	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::C2S_Controls)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	if (size != 5) throw std::runtime_error("Controls message with size " + std::to_string(size) + " != 5!");
	
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	auto recv_button = [](uint8_t byte, Button *button) {
		button->pressed = (byte & 0x80);
		uint32_t d = uint32_t(button->downs) + uint32_t(byte & 0x7f);
		if (d > 255) {
			std::cerr << "got a whole lot of downs" << std::endl;
			d = 255;
		}
		button->downs = uint8_t(d);
	};

	recv_button(recv_buffer[4+0], &left);
	recv_button(recv_buffer[4+1], &right);
	recv_button(recv_buffer[4+2], &up);
	recv_button(recv_buffer[4+3], &down);
	recv_button(recv_buffer[4+4], &jump);

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}


//-----------------------------------------


bool spawnApples(uint32_t applesToPlace) {
	if(applesToPlace > 1) {
		std::list<ivec2> valid_apple_spawns = valid_spawn_positions();

		for(uint32_t i = 0; (i < applesToPlace) && (valid_apple_spawns.size() > 0); i++) {
			uint32_t idx = mt() % valid_apple_spawns.size();
			auto new_it = *std::advance(valid_apple_spawns.begin(), idx);
			glm::ivec2 new_pos = *new_it;
			apples.emplace_back(glm::ivec3(new_pos.x, new_pos.y, 0), Normal);
			valid_apple_spawns.erase(new_it);
		}

	}

	if(valid_apple_spawns.size() == 0) {
			return false
	}
	return true;
}

Game::Game() : mt(0x15466666) {
	//spawn first apple
	spawnApples(1);

}

Player *Game::spawn_player() {
	players.emplace_back();
	Player &player = players.back();

	//random point in the middle area of the arena:
	player.block_positions.emplace_back(glm::ivec3(0, 0, 0));

	std::list<glm::ivec2> spawn_positions = valid_spawn_positions();
	glm::ivec2 spawn_pos = spawn_positions.size() > 0 ? *std::advance(spawn_positions.begin(), mt() % valid_spawn_positions.size()) : spawn_pos;
	player.block_positions[player.block_positions.size() - 1] = glm::ivec3(spawn_pos.x, spawn_pos.y, 0);


	do {
		player.color.r = mt() / float(mt.max());
		player.color.g = mt() / float(mt.max());
		player.color.b = mt() / float(mt.max());
	} while (player.color == glm::vec3(0.0f));
	player.color = glm::normalize(player.color);

	player.name = "Player " + std::to_string(next_player_number++);

	return &player;
}

void Game::remove_player(Player *player) {
	bool found = false;
	for (auto pi = players.begin(); pi != players.end(); ++pi) {
		if (&*pi == player) {
			players.erase(pi);
			found = true;
			break;
		}
	}
	assert(found);
}

std::list<ivec2> Game::valid_spawn_positions() {
	std::list<ivec2> valid_spawns;
	for(int y = 0; y < height; y++) {
			for(int x = 0; x < width; x++) {
				if(map.grid_idx(x, y) != B) {
					valid_spawns.emplace_back(glm::ivec2(x, y));
				}
			}
		}

		for (auto &p1 : players) {
			for(auto block : p1.block_positions) {
				if (block.z > 0) continue;
				valid_spawns.remove(block.xy);
			}
		}

		for (auto &a : apples) {
			valid_spawns.remove(a.xy);
		}

	return valid_spawns;
}


void Game::update(float elapsed) {
	//position/velocity update:
	int32_t applesToPlace = 0;
	for (auto &p : players) {

		auto move_valid = [&](Button move_dir) {
			if(p.block_positions.size() <= 1) return true;
			glm::ivec3 h_pos = p.block_position.back();
			glm::ivec3 n_pos = *std::prev(p.block_positions.end(), 2);
			if(move_dir == right && ((h_pos.x == (map.width-1) && n_pos.x != 0) || (n_pos.x <= h_pos.x))) return true;
			if(move_dir == left && ((h_pos.x == 0 && n_pos.x != (map.width-1)) || (n_pos.x >= h_pos.x))) return true;
			if(move_dir == up && ((h_pos.y == (map.height-1) && n_pos.y != 0) || (n_pos.y <= h_pos.y))) return true;
			if(move_dir == down && ((h_pos.y == 0 && n_pos.y != (map.height-1)) || (n_pos.y <= h_pos.y))) return true;
		}

		glm::ivec3 vel = glm::ivec3(0, 0, 0);
		if (p.controls.left.pressed && move_valid(left)) {
			vel = glm::ivec3(-1, 0, 0);
			p.move_dir = left;
		}
		else if (p.controls.right.pressed && move_valid(right)) {
			vel = glm::ivec3(1, 0, 0);
			p.move_dir = right;
		}
		else if (p.controls.down.pressed && move_valid(down)) {
			vel = glm::ivec3(0, -1, 0);
			p.move_dir = down;
		}
		else if (p.controls.up.pressed && move_valid(up)) {
			vel = glm::ivec3(0, 1, 0);
			p.move_dir = up;
		}

		if(p.controls.jump.pressed) {
			p.jumpVelocity = 3.0f*p.velocity;
		} else {
			static constexpr float gravity = 5.0f;
			p.jumpVelocity -= gravity*elapsed;
		}

		p.zHeight += p.jumpVelocity*elapsed;
		if(p.zHeight <= 0.5f) {
			p.zHeight = 0.5f;
			p.jumpVelocity = 0.0f;
		}


		p.next_move_timer -= elapsed;
		while(p.next_move_timer < 0) {
			p.next_move_timer += (1.0f / p.velocity);


			glm::ivec3 new_head_pos = p.block_positions.back() + vel;
			new_head_pos.z = std::static_cast<int>(std::floor(p.zHeight));

			//check for collision with apples
			bool hit_apple = false;
			for(auto it = apples.begin(); it != apples.end(); it++) {
				Apple apple = *it;
				if(apple.position == new_head_pos) {
					hit_apple = true;
					applesToPlace++;
					apples.erase(it);
					break;
				}
			}

			if(hit_apple) {
				p.block_positions.emplace_back(new_head_pos);
			} else {
				//upadte block positions
				size_t b_size = p.block_positions.size();
				for(size_t i = 0; i < b_size-1; i++) {
					p.block_positions[i] = p.block_positions[i+1];	
				}
				p.block_positions[b_size-1] = new_head_pos;
			}
		}


		//reset 'downs' since controls have been handled:
		p.controls.left.downs = 0;
		p.controls.right.downs = 0;
		p.controls.up.downs = 0;
		p.controls.down.downs = 0;
		p.controls.jump.downs = 0;
	}

	//collision resolution:
	for (auto &p1 : players) {
		glm::ivec3 head_pos = p1.block_positions.back();
		//player/player collisions:
		for (auto &p2 : players) {
			//head / player collisions
			for(auto block : p2.block_positions) {
				if(head_pos == block);
				//kill p1 !
				p1.alive = false;
				break;
			}
		}

		//player grid collisions
		if(map.grid(head_pos.x, head_pos.y) == B) {
			p1.alive = false;
		}
		
	}


	bool gameOver = spawnApples(applesToPlace);
	
}


void Game::send_state_message(Connection *connection_, Player *connection_player) const {
	assert(connection_);
	auto &connection = *connection_;

	connection.send(Message::S2C_State);
	//will patch message size in later, for now placeholder bytes:
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	size_t mark = connection.send_buffer.size(); //keep track of this position in the buffer


	//send player info helper:
	auto send_player = [&](Player const &player) {
		connection.send(player.color);
		connection.send(player.move_dir);
	
		uint32_t len = uint32_t(player.block_positions.size());
		connection.send(len);
		for(uint32_t i = 0; i < player.block_positions.size(); i++) {
			connection.send(player.block_positions[i]);
		}

		//NOTE: can't just 'send(name)' because player.name is not plain-old-data type.
		//effectively: truncates player name to 255 chars
		uint8_t name_len = uint8_t(std::min< size_t >(255, player.name.size()));
		connection.send(name_len);
		connection.send_buffer.insert(connection.send_buffer.end(), player.name.begin(), player.name.begin() + name_len);
	};

	//TODO change to only send when player joins?
	//map count
	connection.send(uint32_t(map.width));
	connection.send(uint32_t(map.height));
	connection.send_buffer.insert(connection.send_buffer.end(), player.name.begin(), map.blocks.begin() + width*height);

	//apple count
	connections.send(uint32_t(apples.size()));
	for(uint32_t i = 0; i < apples.size(); i++) {
		connection.send(apples[i]);
	}

	//player count:
	connection.send(uint8_t(players.size()));
	if (connection_player) send_player(*connection_player);
	for (auto const &player : players) {
		if (&player == connection_player) continue;
		send_player(player);
	}

	//compute the message size and patch into the message header:
	uint32_t size = uint32_t(connection.send_buffer.size() - mark);
	connection.send_buffer[mark-3] = uint8_t(size);
	connection.send_buffer[mark-2] = uint8_t(size >> 8);
	connection.send_buffer[mark-1] = uint8_t(size >> 16);
}

bool Game::recv_state_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;
	auto &recv_buffer = connection.recv_buffer;

	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::S2C_State)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	uint32_t at = 0;
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	//copy bytes from buffer and advance position:
	auto read = [&](auto *val) {
		if (at + sizeof(*val) > size) {
			throw std::runtime_error("Ran out of bytes reading state message.");
		}
		std::memcpy(val, &recv_buffer[4 + at], sizeof(*val));
		at += sizeof(*val);
	};

	read(&map.width);
	read(&map.height);
	if(map.blocks.size <= 0) map.blocks.reserve(width*height);
	for(uint32_t i = 0; i < map.width*map.height; i++) {
		MapBlock m;
		read(&m);
		map.blocks[i] = m;
	}

	uint32_t applesSize;
	read(&applesSize);
	apples.clear();
	apples.reserve(applesSize);
	for(uint32_t i = 0; i < applesSize; i++) {
		Apple a;
		read(&a);
		apples[i] = a;
	}

	players.clear();
	uint8_t player_count;
	read(&player_count);
	for (uint8_t i = 0; i < player_count; ++i) {
		players.emplace_back();
		Player &player = players.back();
		read(&player.position);
		read(&player.velocity);
		read(&player.color);

		uomt8_t block_positions_len;
		read(&block_positions_len);
		player.block_positions = std::vector<glm::uvec2>;
		for (uint8_t n = 0; n < name_len; ++n) {
			char c;
			read(&c);
			player.name += c;
		}


		uint8_t name_len;
		read(&name_len);
		//n.b. would probably be more efficient to directly copy from recv_buffer, but I think this is clearer:
		player.name = "";
		for (uint8_t n = 0; n < name_len; ++n) {
			char c;
			read(&c);
			player.name += c;
		}

	}

	if (at != size) throw std::runtime_error("Trailing data in state message.");

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}
